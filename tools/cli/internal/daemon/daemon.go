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
	"unicode/utf8"

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
	commandTimeout = 10 * time.Second
)

var ErrDisconnected = errors.New("device disconnected")
var ErrAlreadyRunning = errors.New("daemon already running")
var ErrEvalInterrupted = errors.New("device eval interrupted")

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
	// bytes acquire this to prevent interleaving on the wire.
	writeMu sync.Mutex

	// One FROTH-LINK/1 transaction at a time
	reqMu    sync.Mutex
	reqIDSeq atomic.Uint32

	// Per-request waiter. Before sending a frame, the caller registers
	// a waiter with the expected request ID. The serial reader delivers
	// matching frames directly. No buffering, no drain, no race.
	waiterMu sync.Mutex
	waiterID interruptibleWaiter
	waiterCh chan frameResponse // delivery channel, nil = no waiter

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
	requestID     uint16
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

// transportReadLoop reads from the active transport, classifies bytes as
// console text or COBS frames, and delivers decoded frames to replyCh.
func (d *Daemon) transportReadLoop() {
	defer d.wg.Done()

	buf := make([]byte, 1)
	var frame []byte
	var consoleBuf []byte
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
			// Flush console buffer on idle.
			d.flushConsoleBuffer(consoleBuf)
			consoleBuf = consoleBuf[:0]
			continue
		}

		b := buf[0]
		if b == 0x00 {
			// Flush console buffer before frame processing
			d.flushConsoleBuffer(consoleBuf)
			consoleBuf = consoleBuf[:0]
			if inFrame {
				if len(frame) > 0 {
					// Decode and deliver directly to the pending waiter.
					d.deliverFrame(frame)
				}
				frame = frame[:0]
				inFrame = false
				continue
			}
			frame = frame[:0]
			inFrame = true
			continue
		}

		if inFrame {
			frame = append(frame, b)
		} else {
			consoleBuf = append(consoleBuf, b)
			if b == '\n' || len(consoleBuf) >= 256 {
				d.broadcast(EventConsole, &ConsoleEvent{Text: string(consoleBuf)})
				consoleBuf = consoleBuf[:0]
			}
		}
	}
}

func (d *Daemon) flushConsoleBuffer(consoleBuf []byte) {
	if len(consoleBuf) == 0 {
		return
	}

	if d.recoverConsoleFrame(consoleBuf) {
		return
	}

	if summary, ok := suspiciousConsoleSummary(consoleBuf, d.currentWaiter()); ok {
		log.Print(summary)
	}

	d.broadcast(EventConsole, &ConsoleEvent{Text: string(consoleBuf)})
}

func (d *Daemon) recoverConsoleFrame(consoleBuf []byte) bool {
	waiter := d.currentWaiter()
	if waiter.requestID == 0 {
		return false
	}

	decoded, err := protocol.COBSDecode(consoleBuf)
	if err != nil {
		return false
	}

	header, payload, err := protocol.ParseFrame(decoded)
	if err != nil {
		return false
	}

	if header.RequestID != waiter.requestID {
		return false
	}

	log.Printf(
		"console: recovered leaked %s from console stream (id=%d)",
		msgTypeName(header.MessageType),
		header.RequestID,
	)

	d.waiterMu.Lock()
	ch := d.waiterCh
	d.waiterMu.Unlock()
	if ch == nil {
		return false
	}

	select {
	case ch <- frameResponse{header: header, payload: payload}:
		return true
	default:
		log.Printf(
			"console: recovered %s but waiter channel was full (id=%d)",
			msgTypeName(header.MessageType),
			header.RequestID,
		)
		return false
	}
}

func (d *Daemon) currentWaiter() interruptibleWaiter {
	d.waiterMu.Lock()
	defer d.waiterMu.Unlock()
	return d.waiterID
}

func suspiciousConsoleSummary(consoleBuf []byte, waiter interruptibleWaiter) (string, bool) {
	if looksLikeConsoleText(consoleBuf) {
		return "", false
	}

	preview := consoleBuf
	suffix := ""
	if len(preview) > 24 {
		preview = preview[:24]
		suffix = " ..."
	}

	waiterSummary := "none"
	if waiter.requestID != 0 {
		waiterSummary = fmt.Sprintf("%s id=%d", msgTypeName(waiter.messageType), waiter.requestID)
	}

	return fmt.Sprintf(
		"console: suspicious binary burst (%d bytes, waiter=%s): % x%s",
		len(consoleBuf),
		waiterSummary,
		preview,
		suffix,
	), true
}

func looksLikeConsoleText(buf []byte) bool {
	if len(buf) == 0 {
		return true
	}
	if !utf8.Valid(buf) {
		return false
	}

	printable := 0
	for _, b := range buf {
		switch {
		case b == '\n', b == '\r', b == '\t':
			printable++
		case b >= 0x20 && b <= 0x7e:
			printable++
		}
	}

	// Human REPL/console output should be overwhelmingly printable.
	return printable*100 >= len(buf)*90
}

// deliverFrame decodes a raw COBS frame and delivers it to the
// registered waiter if the request ID matches. Unmatched or corrupt
// frames are logged and dropped.
func (d *Daemon) deliverFrame(raw []byte) {
	decoded, err := protocol.COBSDecode(raw)
	if err != nil {
		log.Printf("frame: corrupt COBS (%v)", err)
		return
	}

	header, payload, err := protocol.ParseFrame(decoded)
	if err != nil {
		log.Printf("frame: bad header (%v)", err)
		return
	}

	d.waiterMu.Lock()
	ch := d.waiterCh
	waiter := d.waiterID
	d.waiterMu.Unlock()

	if ch == nil {
		log.Printf("frame: no waiter, dropping %s (id=%d)", msgTypeName(header.MessageType), header.RequestID)
		return
	}

	if header.RequestID != waiter.requestID {
		log.Printf("frame: stale (got id=%d, want %d), dropping", header.RequestID, waiter.requestID)
		return
	}

	// Deliver to the waiting goroutine. Non-blocking because the channel
	// has capacity 1 and only one waiter exists at a time (reqMu).
	select {
	case ch <- frameResponse{header: header, payload: payload}:
	default:
		log.Printf("frame: waiter channel full, dropping %s (id=%d)", msgTypeName(header.MessageType), header.RequestID)
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
	// Signal any blocked waitResponse
	close(d.disconnectCh)
	d.portMu.Unlock()

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
func (d *Daemon) sendFrame(msgType byte, reqID uint16, payload []byte) error {
	wire, err := protocol.EncodeWireFrame(msgType, reqID, payload)
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

// registerWaiter sets up a per-request delivery channel. The serial
// reader will deliver matching frames here. Must be called before
// sending the frame. The caller holds reqMu so only one waiter exists.
func (d *Daemon) registerWaiter(reqID uint16, messageType byte, interruptible bool) chan frameResponse {
	ch := make(chan frameResponse, 1)
	d.waiterMu.Lock()
	d.waiterID = interruptibleWaiter{
		messageType:   messageType,
		requestID:     reqID,
		interruptible: interruptible,
	}
	d.waiterCh = ch
	d.waiterMu.Unlock()
	return ch
}

// clearWaiter removes the registered waiter. Called after waitResponse
// returns, whether success or error.
func (d *Daemon) clearWaiter() {
	d.waiterMu.Lock()
	d.waiterID = interruptibleWaiter{}
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
func (d *Daemon) deviceEval(source string) (*EvalResult, error) {
	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()
	if conn == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	chunks, err := session.ChunkEvalSource(source)
	if err != nil {
		return nil, err
	}
	var lastResult *EvalResult

	for _, chunk := range chunks {
		reqID := d.allocReqID()
		payload := protocol.BuildEvalPayload(chunk)

		ch := d.registerWaiter(reqID, protocol.EvalReq, true)
		if err := d.sendFrame(protocol.EvalReq, reqID, payload); err != nil {
			d.clearWaiter()
			return nil, fmt.Errorf("write: %w", err)
		}

		header, respPayload, err := d.waitResponse(ch, 0)
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

	reqID := d.allocReqID()
	ch := d.registerWaiter(reqID, protocol.InfoReq, false)
	if err := d.sendFrame(protocol.InfoReq, reqID, nil); err != nil {
		d.clearWaiter()
		return nil, fmt.Errorf("write: %w", err)
	}

	header, respPayload, err := d.waitResponse(ch, commandTimeout)
	if err != nil {
		return nil, err
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

	reqID := d.allocReqID()
	ch := d.registerWaiter(reqID, protocol.ResetReq, false)
	if err := d.sendFrame(protocol.ResetReq, reqID, nil); err != nil {
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

// deviceInterrupt sends a raw 0x03 (Ctrl-C) byte to the device.
// Uses writeMu (not reqMu) so it can execute while eval is in progress.
func (d *Daemon) deviceInterrupt() error {
	d.writeMu.Lock()
	defer d.writeMu.Unlock()

	d.portMu.Lock()
	conn := d.conn
	d.portMu.Unlock()

	if conn == nil {
		return ErrDisconnected
	}

	if err := conn.Write([]byte{0x03}); err != nil {
		return err
	}

	d.cancelInterruptibleWaiter(ErrEvalInterrupted)
	return nil
}

// allocReqID returns a unique request ID in the range [1, 0xFFFE].
func (d *Daemon) allocReqID() uint16 {
	id := d.reqIDSeq.Add(1)
	return uint16((id % 0xFFFE) + 1)
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
	case protocol.EvalReq:
		return "EVAL_REQ"
	case protocol.EvalRes:
		return "EVAL_RES"
	case protocol.InspectReq:
		return "INSPECT_REQ"
	case protocol.InspectRes:
		return "INSPECT_RES"
	case protocol.InfoReq:
		return "INFO_REQ"
	case protocol.InfoRes:
		return "INFO_RES"
	case protocol.ResetReq:
		return "RESET_REQ"
	case protocol.ResetRes:
		return "RESET_RES"
	case protocol.Error:
		return "ERROR"
	default:
		return fmt.Sprintf("0x%02x", t)
	}
}
