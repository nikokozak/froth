package daemon

import (
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
	"github.com/nikokozak/froth/tools/cli/internal/serial"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

const (
	reconnectInterval = 2 * time.Second
	serialReadTimeout = 100 * time.Millisecond
	// Safety timeout for non-eval operations (info, reset, hello).
	// These should respond within milliseconds. 10s catches transport
	// failures (malformed frame dropped by device, no reply) without
	// interfering with normal operation. Eval uses 0 (no timeout).
	commandTimeout        = 10 * time.Second
	keepaliveInterval     = 2 * time.Second
	attachTimeout         = 5 * time.Second
	shutdownDetachTimeout = 1 * time.Second
	maxAttachRetries      = 3
	attachRetryDelay      = 500 * time.Millisecond
	waiterBufferSize      = 8
	maxEncodedFrameSize   = protocol.HeaderSize + protocol.MaxPayload +
		((protocol.HeaderSize + protocol.MaxPayload) / 254) + 1
)

var ErrDisconnected = errors.New("device disconnected")
var ErrAlreadyRunning = errors.New("daemon already running")
var ErrEvalInterrupted = errors.New("device eval interrupted")
var interruptCancelTimeout = 5 * time.Second

type transport = serial.Transport

type localTransport struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout io.ReadCloser

	path string

	readCh chan byte
	done   chan struct{}

	stateMu     sync.Mutex
	readTimeout time.Duration
	readErr     error

	closeOnce sync.Once
}

// frameResponse is a decoded frame delivered by the serial reader.
type frameResponse struct {
	header  *protocol.Header
	payload []byte
	err     error
}

// Daemon owns a serial connection and multiplexes RPC access for
// CLI and editor clients over a Unix domain socket.
type Daemon struct {
	portPath         string
	socketPath       string
	pidPath          string
	local            bool
	localRuntimePath string

	// Active transport (guarded by portMu)
	conn   transport
	hello  *protocol.HelloResponse
	portMu sync.Mutex

	// Serial write serialization. Both framed writes and raw interrupt
	// writes acquire this to prevent interleaving on the wire.
	writeMu sync.Mutex

	// One FROTH-LINK/2 transaction at a time.
	reqMu sync.Mutex

	// Live session state. reqMu serializes attach/detach and normal requests.
	nextSeq uint16

	// KEEPALIVE ticker (nil when not attached, guarded by reqMu)
	keepaliveTicker *time.Ticker
	keepaliveStop   chan struct{}

	// Per-request waiter. Before sending a frame, the caller registers
	// a waiter with the expected sequence number. The serial reader delivers
	// matching frames directly. Also guards the active live-session identity
	// needed by the read loop and interrupt/input side paths.
	waiterMu          sync.Mutex
	attached          bool
	sessionID         uint64
	activeSeq         uint16
	waiterSessionID   uint64
	evalOwner         *rpcConn
	interruptCancel   chan struct{}
	interruptWatchSeq uint16
	waiterID          interruptibleWaiter
	waiterCh          chan frameResponse // delivery channel, nil = no waiter

	// Closed by handleDisconnect to unblock any waiting request.
	disconnectCh chan struct{}

	// Connected RPC clients
	clients   map[*rpcConn]struct{}
	clientsMu sync.Mutex

	// Lifecycle
	listener     net.Listener
	done         chan struct{}
	closeOnce    sync.Once
	reconnecting atomic.Bool
	wg           sync.WaitGroup
}

type interruptibleWaiter struct {
	messageType   byte
	seq           uint16
	interruptible bool
}

func New(portPath string, local bool, localRuntimePath string) *Daemon {
	frothDir := frothStateDir()

	return &Daemon{
		portPath:         portPath,
		socketPath:       filepath.Join(frothDir, "daemon.sock"),
		pidPath:          filepath.Join(frothDir, "daemon.pid"),
		local:            local,
		localRuntimePath: localRuntimePath,
		disconnectCh:     make(chan struct{}),
		clients:          make(map[*rpcConn]struct{}),
		done:             make(chan struct{}),
	}
}

func (d *Daemon) sessionSnapshot() (attached bool, sessionID uint64, activeSeq uint16) {
	d.waiterMu.Lock()
	defer d.waiterMu.Unlock()
	return d.attached, d.sessionID, d.activeSeq
}

func (d *Daemon) setSessionState(attached bool, sessionID uint64, activeSeq uint16) {
	d.waiterMu.Lock()
	d.attached = attached
	d.sessionID = sessionID
	d.activeSeq = activeSeq
	if !attached {
		if d.interruptCancel != nil {
			close(d.interruptCancel)
			d.interruptCancel = nil
		}
		d.evalOwner = nil
		d.waiterSessionID = 0
		d.interruptWatchSeq = 0
	}
	d.waiterMu.Unlock()
}

func (d *Daemon) beginActiveEval(seq uint16, owner *rpcConn) {
	d.waiterMu.Lock()
	if d.interruptCancel != nil {
		close(d.interruptCancel)
	}
	d.activeSeq = seq
	d.evalOwner = owner
	d.interruptCancel = make(chan struct{})
	d.interruptWatchSeq = 0
	d.waiterMu.Unlock()
}

func (d *Daemon) endActiveEval() {
	d.waiterMu.Lock()
	d.activeSeq = 0
	d.evalOwner = nil
	if d.interruptCancel != nil {
		close(d.interruptCancel)
		d.interruptCancel = nil
	}
	d.interruptWatchSeq = 0
	d.waiterMu.Unlock()
}

func (d *Daemon) isEvalOwner(c *rpcConn) bool {
	d.waiterMu.Lock()
	defer d.waiterMu.Unlock()
	return d.evalOwner == c
}

// SocketPath returns the Unix socket path for client connections.
func SocketPath() string {
	return filepath.Join(frothStateDir(), "daemon.sock")
}

// PIDPath returns the path to the daemon's PID file.
func PIDPath() string {
	return filepath.Join(frothStateDir(), "daemon.pid")
}

func newLocalTransport(runtimePath string) (*localTransport, error) {
	binary, err := findLocalBinary(runtimePath)
	if err != nil {
		return nil, err
	}

	cmd := exec.Command(binary)

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("stdin pipe: %w", err)
	}

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("stdout pipe: %w", err)
	}

	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start local target: %w", err)
	}

	t := &localTransport{
		cmd:    cmd,
		stdin:  stdin,
		stdout: stdout,
		path:   "stdin/stdout",
		readCh: make(chan byte, 4096),
		done:   make(chan struct{}),
	}

	go t.readLoop()
	go t.waitLoop()

	return t, nil
}

func findLocalBinary(runtimePath string) (string, error) {
	if runtimePath != "" {
		if st, err := os.Stat(runtimePath); err == nil && !st.IsDir() {
			return filepath.Abs(runtimePath)
		}
		return "", fmt.Errorf("local Froth binary not found at %s", runtimePath)
	}

	if st, err := os.Stat(filepath.Join(".", "build64", "Froth")); err == nil && !st.IsDir() {
		return filepath.Abs(filepath.Join(".", "build64", "Froth"))
	}

	if p, err := exec.LookPath("Froth"); err == nil {
		return p, nil
	}

	if p, err := exec.LookPath("froth"); err == nil {
		if exe, exeErr := os.Executable(); exeErr == nil {
			candidate, candErr := filepath.EvalSymlinks(p)
			current, currentErr := filepath.EvalSymlinks(exe)
			if candErr == nil && currentErr == nil && candidate == current {
				return "", fmt.Errorf(
					"local Froth binary not found (froth on PATH resolves to the CLI, not the POSIX runtime)",
				)
			}
		}
		return p, nil
	}

	return "", fmt.Errorf("local Froth binary not found (expected ./build64/Froth or Froth on PATH)")
}

func (t *localTransport) readLoop() {
	buf := make([]byte, 256)

	for {
		n, err := t.stdout.Read(buf)
		if n > 0 {
			for _, b := range buf[:n] {
				select {
				case t.readCh <- b:
				case <-t.done:
					return
				}
			}
		}

		if err != nil {
			t.signalErr(err)
			return
		}
	}
}

func (t *localTransport) waitLoop() {
	err := t.cmd.Wait()
	if err != nil {
		t.signalErr(err)
		return
	}
	t.signalErr(io.EOF)
}

func (t *localTransport) signalErr(err error) {
	t.stateMu.Lock()
	if t.readErr == nil {
		t.readErr = err
	}
	t.stateMu.Unlock()

	select {
	case <-t.done:
	default:
		close(t.done)
	}
}

func (t *localTransport) Read(buf []byte) (int, error) {
	timeout := t.currentReadTimeout()

	if timeout > 0 {
		timer := time.NewTimer(timeout)
		defer timer.Stop()

		select {
		case b := <-t.readCh:
			buf[0] = b
			return 1, nil
		case <-timer.C:
			return 0, nil
		case <-t.done:
			return 0, t.currentReadErr()
		}
	}

	select {
	case b := <-t.readCh:
		buf[0] = b
		return 1, nil
	case <-t.done:
		return 0, t.currentReadErr()
	}
}

func (t *localTransport) Write(data []byte) error {
	n, err := t.stdin.Write(data)
	if err != nil {
		return fmt.Errorf("local write: %w", err)
	}
	if n != len(data) {
		return fmt.Errorf("short write: wrote %d of %d bytes", n, len(data))
	}
	return nil
}

func (t *localTransport) Close() error {
	var result error

	t.closeOnce.Do(func() {
		select {
		case <-t.done:
		default:
			close(t.done)
		}

		if t.stdin != nil {
			_ = t.stdin.Close()
		}
		if t.stdout != nil {
			_ = t.stdout.Close()
		}
		if t.cmd.Process != nil {
			if err := t.cmd.Process.Kill(); err != nil && !errors.Is(err, os.ErrProcessDone) {
				result = err
			}
		}
	})

	return result
}

func (t *localTransport) Path() string {
	return t.path
}

func (t *localTransport) SetReadTimeout(d time.Duration) error {
	t.stateMu.Lock()
	t.readTimeout = d
	t.stateMu.Unlock()
	return nil
}

func (t *localTransport) ResetInputBuffer() {
	for {
		select {
		case <-t.readCh:
		default:
			return
		}
	}
}

func (t *localTransport) Drain(duration time.Duration) {
	deadline := time.Now().Add(duration)
	for time.Now().Before(deadline) {
		remaining := time.Until(deadline)
		if remaining > 10*time.Millisecond {
			remaining = 10 * time.Millisecond
		}
		select {
		case <-t.readCh:
		case <-time.After(remaining):
		}
	}
}

func (t *localTransport) currentReadTimeout() time.Duration {
	t.stateMu.Lock()
	defer t.stateMu.Unlock()
	return t.readTimeout
}

func (t *localTransport) currentReadErr() error {
	t.stateMu.Lock()
	defer t.stateMu.Unlock()
	if t.readErr != nil {
		return t.readErr
	}
	return io.EOF
}

func frothStateDir() string {
	dir, err := sdk.FrothHome()
	if err != nil || dir == "" {
		return ".froth"
	}
	return dir
}

// Start runs the daemon in the foreground until interrupted.
func (d *Daemon) Start() error {
	if err := os.MkdirAll(frothStateDir(), 0755); err != nil {
		return fmt.Errorf("create froth dir: %w", err)
	}

	if err := d.prepareSocketPath(); err != nil {
		return err
	}

	ln, err := net.Listen("unix", d.socketPath)
	if err != nil {
		return fmt.Errorf("listen: %w", err)
	}
	d.listener = ln
	defer ln.Close()
	defer os.Remove(d.socketPath)

	if err := os.WriteFile(d.pidPath, []byte(fmt.Sprintf("%d", os.Getpid())), 0644); err != nil {
		return fmt.Errorf("write pid file: %w", err)
	}
	defer os.Remove(d.pidPath)

	log.Printf("socket: %s", d.socketPath)

	d.wg.Add(1)
	go d.acceptLoop()

	if err := d.connect(); err != nil {
		log.Printf("device: %v (will retry)", err)
		d.reconnecting.Store(true)
		d.wg.Add(1)
		go d.reconnectLoop()
	} else {
		d.wg.Add(1)
		go d.transportReadLoop()
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	select {
	case sig := <-sigCh:
		log.Printf("shutdown: %v", sig)
	case <-d.done:
	}

	d.shutdown()
	return nil
}

func (d *Daemon) prepareSocketPath() error {
	if _, err := os.Stat(d.socketPath); err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return fmt.Errorf("stat socket: %w", err)
	}

	client, err := DialPath(d.socketPath)
	if err == nil {
		defer client.Close()
		if _, statusErr := client.Status(); statusErr == nil {
			return ErrAlreadyRunning
		}
	}

	if err := os.Remove(d.socketPath); err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("remove stale socket: %w", err)
	}
	return nil
}

func (d *Daemon) connect() error {
	var conn transport
	var hello *protocol.HelloResponse
	var err error

	if d.local {
		conn, hello, err = connectLocal(d.localRuntimePath)
		if err != nil {
			return err
		}
	} else if d.portPath != "" {
		var port *serial.Port
		port, hello, err = serial.OpenAndProbe(d.portPath)
		if err != nil {
			return fmt.Errorf("connect %s: %w", d.portPath, err)
		}
		conn = port
	} else {
		var port *serial.Port
		port, hello, err = serial.Discover()
		if err != nil {
			return err
		}
		conn = port
	}

	d.portMu.Lock()
	d.conn = conn
	d.hello = hello
	d.portPath = conn.Path()
	// Fresh disconnect channel for this connection
	d.disconnectCh = make(chan struct{})
	d.portMu.Unlock()

	log.Printf("device: %s on %s (%d-bit) at %s", hello.Version, hello.Board, hello.CellBits, conn.Path())
	d.broadcast(EventConnected, &ConnectedEvent{
		Device: helloToResult(hello),
		Port:   conn.Path(),
	})

	return nil
}

func connectLocal(runtimePath string) (transport, *protocol.HelloResponse, error) {
	conn, err := newLocalTransport(runtimePath)
	if err != nil {
		return nil, nil, err
	}

	conn.Drain(serial.DrainDuration)

	hello, err := serial.ProbeHelloTransport(conn)
	if err != nil {
		conn.Close()
		return nil, nil, fmt.Errorf("handshake: %w", err)
	}

	conn.ResetInputBuffer()
	return conn, hello, nil
}

// transportReadLoop reads COBS frames from the active transport.
// Non-frame bytes are discarded in both Direct and Live modes.
func (d *Daemon) transportReadLoop() {
	defer d.wg.Done()

	buf := make([]byte, 1)
	frameBuf := make([]byte, 0, maxEncodedFrameSize)
	inFrame := false

	for {
		select {
		case <-d.done:
			return
		default:
		}

		d.portMu.Lock()
		conn := d.conn
		d.portMu.Unlock()

		if conn == nil {
			return
		}

		if err := conn.SetReadTimeout(serialReadTimeout); err != nil {
			d.handleDisconnect(err)
			return
		}
		n, err := conn.Read(buf)
		if err != nil {
			d.handleDisconnect(err)
			return
		}
		if n == 0 {
			continue
		}

		b := buf[0]
		if b == 0x00 {
			if inFrame && len(frameBuf) > 0 {
				d.handleFrame(frameBuf)
			}
			frameBuf = frameBuf[:0]
			inFrame = true
			continue
		}

		if inFrame {
			if len(frameBuf) >= maxEncodedFrameSize {
				frameBuf = frameBuf[:0]
				inFrame = false
				continue
			}
			frameBuf = append(frameBuf, b)
		}
		// Non-frame bytes are discarded.
	}
}

// handleFrame decodes a COBS frame and dispatches it.
func (d *Daemon) handleFrame(cobsData []byte) {
	decoded, err := protocol.COBSDecode(cobsData)
	if err != nil {
		return
	}

	header, payload, err := protocol.ParseFrame(decoded)
	if err != nil {
		return
	}

	attached, sessionID, activeSeq := d.sessionSnapshot()
	if attached && header.SessionID != sessionID {
		return
	}

	switch header.MessageType {
	case protocol.OutputData:
		if !attached || activeSeq == 0 || header.Seq != activeSeq {
			return
		}
		data, err := protocol.ParseOutputData(payload)
		if err != nil {
			return
		}
		d.waiterMu.Lock()
		owner := d.evalOwner
		d.waiterMu.Unlock()
		if owner != nil {
			owner.sendNotification(EventConsole, &ConsoleEvent{Data: append([]byte(nil), data...)})
		}
	case protocol.InputWait:
		if !attached || activeSeq == 0 || header.Seq != activeSeq {
			return
		}
		reason, err := protocol.ParseInputWait(payload)
		if err != nil {
			return
		}
		d.waiterMu.Lock()
		owner := d.evalOwner
		d.waiterMu.Unlock()
		if owner != nil {
			owner.sendNotification(EventInputWait, &InputWaitEvent{
				Reason: int(reason),
				Seq:    int(header.Seq),
			})
		}
	case protocol.AttachRes, protocol.DetachRes,
		protocol.EvalRes, protocol.InfoRes,
		protocol.ResetRes, protocol.HelloRes, protocol.Error:
		d.waiterMu.Lock()
		ch := d.waiterCh
		waiter := d.waiterID
		waiterSessionID := d.waiterSessionID
		d.waiterMu.Unlock()

		if ch == nil {
			log.Printf("frame: no waiter for %s (seq=%d)", msgTypeName(header.MessageType), header.Seq)
			return
		}

		if header.Seq != waiter.seq {
			log.Printf("frame: seq mismatch (got %d, want %d) for %s", header.Seq, waiter.seq, msgTypeName(header.MessageType))
			return
		}
		if waiterSessionID != 0 && header.SessionID != waiterSessionID {
			log.Printf("frame: session mismatch (got %016x, want %016x) for %s", header.SessionID, waiterSessionID, msgTypeName(header.MessageType))
			return
		}
		if header.MessageType != protocol.Error && header.MessageType != waiter.messageType {
			log.Printf(
				"frame: type mismatch (got %s, want %s) for seq=%d",
				msgTypeName(header.MessageType),
				msgTypeName(waiter.messageType),
				header.Seq,
			)
			return
		}

		select {
		case ch <- frameResponse{header: header, payload: payload}:
		default:
			log.Printf("frame: waiter full for %s (seq=%d)", msgTypeName(header.MessageType), header.Seq)
		}
	default:
	}
}

func (d *Daemon) handleDisconnect(err error) {
	d.portMu.Lock()
	if d.conn == nil {
		d.portMu.Unlock()
		return
	}
	d.conn.Close()
	d.conn = nil
	d.hello = nil
	// Unblock waitResponse before taking reqMu. deviceEval waits with reqMu
	// held, so this ordering avoids deadlocking the read loop on disconnect.
	close(d.disconnectCh)
	d.portMu.Unlock()

	d.reqMu.Lock()
	d.enterDirectMode()
	d.reqMu.Unlock()

	log.Printf("device disconnected: %v", err)
	d.broadcast(EventDisconnected, nil)

	select {
	case <-d.done:
		return
	default:
	}

	if !d.reconnecting.CompareAndSwap(false, true) {
		return
	}

	d.wg.Add(1)
	go d.reconnectLoop()
}

func (d *Daemon) reconnectLoop() {
	defer d.wg.Done()

	for {
		select {
		case <-d.done:
			return
		case <-time.After(reconnectInterval):
		}

		d.broadcast(EventReconnecting, nil)

		if err := d.connect(); err != nil {
			log.Printf("reconnect: %v", err)
			continue
		}

		d.reconnecting.Store(false)
		d.wg.Add(1)
		go d.transportReadLoop()
		return
	}
}

func (d *Daemon) acceptLoop() {
	defer d.wg.Done()

	for {
		nc, err := d.listener.Accept()
		if err != nil {
			select {
			case <-d.done:
				return
			default:
				log.Printf("accept: %v", err)
				continue
			}
		}

		c := newRPCConn(nc, d)
		d.clientsMu.Lock()
		d.clients[c] = struct{}{}
		d.clientsMu.Unlock()

		d.wg.Add(1)
		go func() {
			defer d.wg.Done()
			c.serve()
			d.clientsMu.Lock()
			delete(d.clients, c)
			d.clientsMu.Unlock()
		}()
	}
}

func (d *Daemon) broadcast(event string, params any) {
	d.clientsMu.Lock()
	targets := make([]*rpcConn, 0, len(d.clients))
	for c := range d.clients {
		targets = append(targets, c)
	}
	d.clientsMu.Unlock()

	for _, c := range targets {
		c.sendNotification(event, params)
	}
}

func (d *Daemon) shutdown() {
	d.reqMu.Lock()
	if attached, _, _ := d.sessionSnapshot(); attached {
		_ = d.detachWithTimeout(shutdownDetachTimeout)
	}
	d.reqMu.Unlock()

	d.closeOnce.Do(func() { close(d.done) })
	d.listener.Close()

	d.portMu.Lock()
	if d.conn != nil {
		d.conn.Close()
		d.conn = nil
	}
	d.portMu.Unlock()

	d.clientsMu.Lock()
	for c := range d.clients {
		c.close()
	}
	d.clientsMu.Unlock()

	d.wg.Wait()
}

// --- Device operations ---

// sendFrame acquires writeMu, builds and writes a COBS frame.
func (d *Daemon) sendFrame(msgType byte, seq uint16, payload []byte) error {
	_, sessionID, _ := d.sessionSnapshot()

	wire, err := protocol.EncodeWireFrame(sessionID, msgType, seq, payload)
	if err != nil {
		return fmt.Errorf("build frame: %w", err)
	}

	d.writeMu.Lock()
	defer d.writeMu.Unlock()

	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()

	if conn == nil {
		return ErrDisconnected
	}

	return conn.Write(wire)
}

// attach sends ATTACH_REQ and transitions to Live mode.
// Must be called with reqMu held.
func (d *Daemon) attach() error {
	if attached, _, _ := d.sessionSnapshot(); attached {
		return nil
	}

	sessionID, err := protocol.GenerateSessionID()
	if err != nil {
		return err
	}

	for attempt := 0; attempt <= maxAttachRetries; attempt++ {
		if attempt > 0 {
			time.Sleep(attachRetryDelay)
		}

		ch := d.registerWaiter(0, protocol.AttachRes, false)
		d.setWaiterSessionID(sessionID)

		wire, err := protocol.EncodeWireFrame(sessionID, protocol.AttachReq, 0, nil)
		if err != nil {
			d.clearWaiter()
			return err
		}

		d.writeMu.Lock()
		writeErr := func() error {
			d.portMu.Lock()
			conn := d.conn
			d.portMu.Unlock()
			if conn == nil {
				return ErrDisconnected
			}
			return conn.Write(wire)
		}()
		d.writeMu.Unlock()
		if writeErr != nil {
			d.clearWaiter()
			return writeErr
		}

		deadline := time.Now().Add(attachTimeout)

		var payload []byte
		for {
			remaining := time.Until(deadline)
			if remaining <= 0 {
				d.clearWaiter()
				return fmt.Errorf("attach: %w", fmt.Errorf("device response timeout"))
			}

			respHeader, respPayload, err := d.waitResponseNoClear(ch, remaining)
			if err != nil {
				d.clearWaiter()
				return fmt.Errorf("attach: %w", err)
			}
			if respHeader.SessionID != sessionID {
				log.Printf("attach: discard stale session response (%016x != %016x)", respHeader.SessionID, sessionID)
				continue
			}

			payload = respPayload
			break
		}
		d.clearWaiter()

		status, err := protocol.ParseAttachResponse(payload)
		if err != nil {
			return fmt.Errorf("attach: %w", err)
		}

		switch status {
		case protocol.AttachStatusOK:
			d.setSessionState(true, sessionID, 0)
			d.nextSeq = 1
			d.startKeepalive()
			log.Printf("session: attached (id=%016x)", sessionID)
			return nil
		case protocol.AttachStatusBusy:
			log.Printf("session: device busy, retry %d/%d", attempt+1, maxAttachRetries+1)
			continue
		default:
			return fmt.Errorf("attach rejected: status %d", status)
		}
	}

	return fmt.Errorf("attach: device busy after %d retries", maxAttachRetries+1)
}

// detach sends DETACH_REQ and transitions back to Direct mode.
// Must be called with reqMu held.
func (d *Daemon) detach() error {
	return d.detachWithTimeout(commandTimeout)
}

func (d *Daemon) detachWithTimeout(timeout time.Duration) error {
	if attached, _, _ := d.sessionSnapshot(); !attached {
		return nil
	}

	d.stopKeepalive()

	// DETACH uses the current seq without advancing. The session ends here.
	seq := d.nextSeq
	ch := d.registerWaiter(seq, protocol.DetachRes, false)

	if err := d.sendFrame(protocol.DetachReq, seq, nil); err != nil {
		d.clearWaiter()
		d.enterDirectMode()
		return fmt.Errorf("detach write: %w", err)
	}

	_, _, err := d.waitResponse(ch, timeout)
	d.enterDirectMode()
	if err != nil {
		return fmt.Errorf("detach: %w", err)
	}

	log.Printf("session: detached")
	return nil
}

// enterDirectMode clears all Live session state.
func (d *Daemon) enterDirectMode() {
	d.stopKeepalive()
	d.setSessionState(false, 0, 0)
	d.nextSeq = 0
}

func (d *Daemon) startKeepalive() {
	d.stopKeepalive()
	d.keepaliveTicker = time.NewTicker(keepaliveInterval)
	d.keepaliveStop = make(chan struct{})
	go d.keepaliveLoop()
}

func (d *Daemon) stopKeepalive() {
	if d.keepaliveTicker != nil {
		d.keepaliveTicker.Stop()
		if d.keepaliveStop != nil {
			select {
			case <-d.keepaliveStop:
			default:
				close(d.keepaliveStop)
			}
		}
		d.keepaliveTicker = nil
		d.keepaliveStop = nil
	}
}

func (d *Daemon) keepaliveLoop() {
	ticker := d.keepaliveTicker
	stop := d.keepaliveStop
	if ticker == nil || stop == nil {
		return
	}

	for {
		select {
		case <-ticker.C:
			attached, sessionID, _ := d.sessionSnapshot()
			if !attached {
				continue
			}
			wire, err := protocol.EncodeWireFrame(sessionID, protocol.Keepalive, 0, nil)
			if err != nil {
				continue
			}
			d.writeMu.Lock()
			d.portMu.Lock()
			conn := d.conn
			d.portMu.Unlock()
			if conn != nil {
				_ = conn.Write(wire)
			}
			d.writeMu.Unlock()
		case <-stop:
			return
		case <-d.done:
			return
		}
	}
}

// registerWaiter sets up a per-request delivery channel. The serial
// reader will deliver matching frames here. Must be called before
// sending the frame. The caller holds reqMu so only one waiter exists.
func (d *Daemon) registerWaiter(reqID uint16, messageType byte, interruptible bool) chan frameResponse {
	ch := make(chan frameResponse, waiterBufferSize)
	d.waiterMu.Lock()
	d.waiterID = interruptibleWaiter{
		messageType:   messageType,
		seq:           reqID,
		interruptible: interruptible,
	}
	d.waiterSessionID = 0
	d.waiterCh = ch
	d.waiterMu.Unlock()
	return ch
}

func (d *Daemon) setWaiterSessionID(sessionID uint64) {
	d.waiterMu.Lock()
	d.waiterSessionID = sessionID
	d.waiterMu.Unlock()
}

// clearWaiter removes the registered waiter. Called after waitResponse
// returns, whether success or error.
func (d *Daemon) clearWaiter() {
	d.waiterMu.Lock()
	d.waiterID = interruptibleWaiter{}
	d.waiterSessionID = 0
	d.waiterCh = nil
	d.waiterMu.Unlock()
}

func (d *Daemon) cancelInterruptibleWaiter(err error) bool {
	d.waiterMu.Lock()
	ch := d.waiterCh
	waiter := d.waiterID
	d.waiterMu.Unlock()

	if ch == nil || !waiter.interruptible {
		return false
	}

	select {
	case ch <- frameResponse{err: err}:
		return true
	default:
		return false
	}
}

// waitResponse blocks until the serial reader delivers a matching frame,
// the device disconnects, or the daemon shuts down.
// If timeout > 0, gives up after that duration (for info/reset/hello).
// If timeout == 0, waits indefinitely (for eval — programs can run forever).
func (d *Daemon) waitResponse(ch chan frameResponse, timeout time.Duration) (*protocol.Header, []byte, error) {
	defer d.clearWaiter()
	return d.waitResponseNoClear(ch, timeout)
}

func (d *Daemon) waitResponseNoClear(ch chan frameResponse, timeout time.Duration) (*protocol.Header, []byte, error) {

	var timeoutCh <-chan time.Time
	if timeout > 0 {
		timeoutCh = time.After(timeout)
	}

	select {
	case resp := <-ch:
		if resp.err != nil {
			return nil, nil, resp.err
		}
		return resp.header, resp.payload, nil
	case <-d.disconnectCh:
		return nil, nil, ErrDisconnected
	case <-d.done:
		return nil, nil, fmt.Errorf("daemon shutting down")
	case <-timeoutCh:
		return nil, nil, fmt.Errorf("device response timeout")
	}
}

// deviceEval sends source for evaluation. Automatically chunks if needed.
// Blocks until all chunks complete or an error occurs. No timeout.
func (d *Daemon) deviceEval(source string, owner *rpcConn) (*EvalResult, error) {
	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()
	if conn == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	if err := d.attach(); err != nil {
		return nil, fmt.Errorf("eval: %w", err)
	}

	chunks, err := session.ChunkEvalSource(source)
	if err != nil {
		return nil, err
	}
	var lastResult *EvalResult

	for _, chunk := range chunks {
		seq := d.allocSeq()
		d.beginActiveEval(seq, owner)

		payload := protocol.BuildEvalPayload(chunk)

		ch := d.registerWaiter(seq, protocol.EvalRes, true)
		if err := d.sendFrame(protocol.EvalReq, seq, payload); err != nil {
			d.clearWaiter()
			d.endActiveEval()
			return nil, fmt.Errorf("write: %w", err)
		}

		header, respPayload, err := d.waitResponse(ch, 0)
		d.endActiveEval()
		if err != nil {
			return nil, err
		}

		switch header.MessageType {
		case protocol.EvalRes:
			resp, err := protocol.ParseEvalResponse(respPayload)
			if err != nil {
				return nil, err
			}
			lastResult = &EvalResult{
				Status:    int(resp.Status),
				ErrorCode: int(resp.ErrorCode),
				FaultWord: resp.FaultWord,
				StackRepr: resp.StackRepr,
			}
			if lastResult.Status != 0 {
				return lastResult, nil
			}
		case protocol.Error:
			errResp, err := protocol.ParseErrorResponse(respPayload)
			if err != nil {
				return nil, err
			}
			return nil, fmt.Errorf("device error (cat %d): %s", errResp.Category, errResp.Detail)
		default:
			return nil, fmt.Errorf("unexpected response type: 0x%02x", header.MessageType)
		}
	}

	if lastResult == nil {
		return &EvalResult{Status: 0, StackRepr: "[]"}, nil
	}
	return lastResult, nil
}

// deviceInfo sends an INFO_REQ and returns the parsed response.
func (d *Daemon) deviceInfo() (*InfoResult, error) {
	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()
	if conn == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	if err := d.attach(); err != nil {
		return nil, fmt.Errorf("info: %w", err)
	}

	seq := d.allocSeq()
	ch := d.registerWaiter(seq, protocol.InfoRes, false)
	if err := d.sendFrame(protocol.InfoReq, seq, nil); err != nil {
		d.clearWaiter()
		return nil, fmt.Errorf("write: %w", err)
	}

	header, respPayload, err := d.waitResponse(ch, commandTimeout)
	if err != nil {
		return nil, err
	}

	if header.MessageType == protocol.Error {
		errResp, parseErr := protocol.ParseErrorResponse(respPayload)
		if parseErr != nil {
			return nil, parseErr
		}
		return nil, fmt.Errorf("device error (cat %d): %s", errResp.Category, errResp.Detail)
	}

	if header.MessageType != protocol.InfoRes {
		return nil, fmt.Errorf("unexpected response type: 0x%02x", header.MessageType)
	}

	resp, err := protocol.ParseInfoResponse(respPayload)
	if err != nil {
		return nil, err
	}

	return &InfoResult{
		HeapSize:         int(resp.HeapSize),
		HeapUsed:         int(resp.HeapUsed),
		HeapOverlayUsed:  int(resp.HeapOverlayUsed),
		SlotCount:        int(resp.SlotCount),
		SlotOverlayCount: int(resp.SlotOverlayCount),
		Version:          resp.Version,
	}, nil
}

// deviceReset sends a RESET_REQ and returns the parsed response.
func (d *Daemon) deviceReset() (*ResetResult, error) {
	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()
	if conn == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	if err := d.attach(); err != nil {
		return nil, fmt.Errorf("reset: %w", err)
	}

	seq := d.allocSeq()
	ch := d.registerWaiter(seq, protocol.ResetRes, false)
	if err := d.sendFrame(protocol.ResetReq, seq, nil); err != nil {
		d.clearWaiter()
		return nil, fmt.Errorf("write: %w", err)
	}

	header, respPayload, err := d.waitResponse(ch, commandTimeout)
	if err != nil {
		return nil, err
	}

	switch header.MessageType {
	case protocol.ResetRes:
		// handled below
	case protocol.Error:
		errResp, parseErr := protocol.ParseErrorResponse(respPayload)
		if parseErr != nil {
			return nil, parseErr
		}
		return nil, fmt.Errorf("device error (cat %d): %s", errResp.Category, errResp.Detail)
	default:
		return nil, fmt.Errorf("unexpected response type: 0x%02x", header.MessageType)
	}

	resp, err := protocol.ParseResetResponse(respPayload)
	if err != nil {
		return nil, err
	}

	return &ResetResult{
		Status:           int(resp.Status),
		HeapSize:         int(resp.HeapSize),
		HeapUsed:         int(resp.HeapUsed),
		HeapOverlayUsed:  int(resp.HeapOverlayUsed),
		SlotCount:        int(resp.SlotCount),
		SlotOverlayCount: int(resp.SlotOverlayCount),
		Version:          resp.Version,
	}, nil
}

// deviceInterrupt sends a framed INTERRUPT_REQ to the device.
// Uses writeMu (not reqMu) so it can execute while eval is in progress.
func (d *Daemon) deviceInterrupt() error {
	attached, sessionID, activeSeq := d.sessionSnapshot()
	if !attached {
		return fmt.Errorf("not attached")
	}
	if activeSeq == 0 {
		return fmt.Errorf("no active eval to interrupt")
	}

	wire, err := protocol.EncodeWireFrame(sessionID, protocol.InterruptReq, activeSeq, nil)
	if err != nil {
		return err
	}

	d.writeMu.Lock()
	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()
	if conn == nil {
		d.writeMu.Unlock()
		return ErrDisconnected
	}
	writeErr := conn.Write(wire)
	d.writeMu.Unlock()
	if writeErr != nil {
		return writeErr
	}

	d.waiterMu.Lock()
	cancel := (<-chan struct{})(nil)
	if d.activeSeq == activeSeq && d.interruptCancel != nil && d.interruptWatchSeq != activeSeq {
		d.interruptWatchSeq = activeSeq
		cancel = d.interruptCancel
	}
	d.waiterMu.Unlock()

	if cancel == nil {
		return nil
	}

	go func(cancel <-chan struct{}) {
		select {
		case <-time.After(interruptCancelTimeout):
			d.cancelInterruptibleWaiter(ErrEvalInterrupted)
		case <-cancel:
		case <-d.done:
		}
	}(cancel)

	return nil
}

// deviceSendInput sends INPUT_DATA to the active eval sequence.
func (d *Daemon) deviceSendInput(seq uint16, data []byte) error {
	attached, sessionID, activeSeq := d.sessionSnapshot()
	if !attached {
		return fmt.Errorf("not attached")
	}
	if activeSeq == 0 {
		return fmt.Errorf("no active eval")
	}
	if seq != activeSeq {
		return fmt.Errorf("stale input seq %d (active %d)", seq, activeSeq)
	}

	payload := protocol.BuildInputDataPayload(data)
	wire, err := protocol.EncodeWireFrame(sessionID, protocol.InputData, seq, payload)
	if err != nil {
		return err
	}

	d.writeMu.Lock()
	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()
	if conn == nil {
		d.writeMu.Unlock()
		return ErrDisconnected
	}
	writeErr := conn.Write(wire)
	d.writeMu.Unlock()
	return writeErr
}

// allocSeq returns the next seq and advances the counter.
// Must be called with reqMu held.
func (d *Daemon) allocSeq() uint16 {
	seq := d.nextSeq
	d.nextSeq++
	if d.nextSeq == 0 {
		d.nextSeq = 1
	}
	return seq
}

func helloToResult(h *protocol.HelloResponse) HelloResult {
	return HelloResult{
		CellBits:   int(h.CellBits),
		MaxPayload: int(h.MaxPayload),
		HeapSize:   int(h.HeapSize),
		HeapUsed:   int(h.HeapUsed),
		SlotCount:  int(h.SlotCount),
		Version:    h.Version,
		Board:      h.Board,
	}
}

func msgTypeName(t byte) string {
	switch t {
	case protocol.HelloReq:
		return "HELLO_REQ"
	case protocol.HelloRes:
		return "HELLO_RES"
	case protocol.AttachReq:
		return "ATTACH_REQ"
	case protocol.AttachRes:
		return "ATTACH_RES"
	case protocol.DetachReq:
		return "DETACH_REQ"
	case protocol.DetachRes:
		return "DETACH_RES"
	case protocol.EvalReq:
		return "EVAL_REQ"
	case protocol.EvalRes:
		return "EVAL_RES"
	case protocol.InfoReq:
		return "INFO_REQ"
	case protocol.InfoRes:
		return "INFO_RES"
	case protocol.ResetReq:
		return "RESET_REQ"
	case protocol.ResetRes:
		return "RESET_RES"
	case protocol.InterruptReq:
		return "INTERRUPT_REQ"
	case protocol.Keepalive:
		return "KEEPALIVE"
	case protocol.InputData:
		return "INPUT_DATA"
	case protocol.InputWait:
		return "INPUT_WAIT"
	case protocol.OutputData:
		return "OUTPUT_DATA"
	case protocol.Error:
		return "ERROR"
	default:
		return fmt.Sprintf("0x%02x", t)
	}
}
