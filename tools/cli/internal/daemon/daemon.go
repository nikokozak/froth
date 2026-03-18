package daemon

import (
	"errors"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
	"github.com/nikokozak/froth/tools/cli/internal/serial"
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

// frameResponse is a decoded frame delivered by the serial reader.
type frameResponse struct {
	header  *protocol.Header
	payload []byte
	err     error
}

// Daemon owns a serial connection and multiplexes RPC access for
// CLI and editor clients over a Unix domain socket.
type Daemon struct {
	portPath   string
	socketPath string
	pidPath    string

	// Serial connection (guarded by portMu)
	port   *serial.Port
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
	waiterID uint16              // expected request ID, 0 = no waiter
	waiterCh chan frameResponse  // delivery channel, nil = no waiter

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

func New(portPath string) *Daemon {
	home, _ := os.UserHomeDir()
	frothDir := filepath.Join(home, ".froth")

	return &Daemon{
		portPath:     portPath,
		socketPath:   filepath.Join(frothDir, "daemon.sock"),
		pidPath:      filepath.Join(frothDir, "daemon.pid"),
		disconnectCh: make(chan struct{}),
		clients:      make(map[*rpcConn]struct{}),
		done:         make(chan struct{}),
	}
}

// SocketPath returns the Unix socket path for client connections.
func SocketPath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".froth", "daemon.sock")
}

// PIDPath returns the path to the daemon's PID file.
func PIDPath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".froth", "daemon.pid")
}

// Start runs the daemon in the foreground until interrupted.
func (d *Daemon) Start() error {
	home, _ := os.UserHomeDir()
	os.MkdirAll(filepath.Join(home, ".froth"), 0755)

	if err := os.WriteFile(d.pidPath, []byte(fmt.Sprintf("%d", os.Getpid())), 0644); err != nil {
		return fmt.Errorf("write pid file: %w", err)
	}
	defer os.Remove(d.pidPath)

	// Clean stale socket
	os.Remove(d.socketPath)

	ln, err := net.Listen("unix", d.socketPath)
	if err != nil {
		return fmt.Errorf("listen: %w", err)
	}
	d.listener = ln
	defer ln.Close()
	defer os.Remove(d.socketPath)

	log.Printf("socket: %s", d.socketPath)

	if err := d.connect(); err != nil {
		log.Printf("device: %v (will retry)", err)
		d.wg.Add(1)
		go d.reconnectLoop()
	} else {
		d.wg.Add(1)
		go d.serialReadLoop()
	}

	d.wg.Add(1)
	go d.acceptLoop()

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

func (d *Daemon) connect() error {
	var port *serial.Port
	var hello *protocol.HelloResponse
	var err error

	if d.portPath != "" {
		port, err = serial.Open(d.portPath)
		if err != nil {
			return fmt.Errorf("open %s: %w", d.portPath, err)
		}
		port.Drain(serial.DrainDuration)
		hello, err = serial.ProbeHello(port, serial.DefaultProbeTimeout)
		if err != nil {
			port.Close()
			return fmt.Errorf("handshake: %w", err)
		}
	} else {
		port, hello, err = serial.Discover()
		if err != nil {
			return err
		}
	}

	d.portMu.Lock()
	d.port = port
	d.hello = hello
	if d.portPath == "" {
		d.portPath = port.Path()
	}
	// Fresh disconnect channel for this connection
	d.disconnectCh = make(chan struct{})
	d.portMu.Unlock()

	log.Printf("device: %s on %s (%d-bit) at %s", hello.Version, hello.Board, hello.CellBits, port.Path())
	d.broadcast(EventConnected, &ConnectedEvent{
		Device: helloToResult(hello),
		Port:   port.Path(),
	})

	return nil
}

// serialReadLoop reads from the serial port, classifies bytes as
// console text or COBS frames, and delivers decoded frames to replyCh.
func (d *Daemon) serialReadLoop() {
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
		port := d.port
		d.portMu.Unlock()

		if port == nil {
			return
		}

		if err := port.SetReadTimeout(serialReadTimeout); err != nil {
			d.handleDisconnect(err)
			return
		}
		n, err := port.Read(buf)
		if err != nil {
			d.handleDisconnect(err)
			return
		}
		if n == 0 {
			// Flush console buffer on idle
			if len(consoleBuf) > 0 {
				d.broadcast(EventConsole, &ConsoleEvent{Text: string(consoleBuf)})
				consoleBuf = consoleBuf[:0]
			}
			continue
		}

		b := buf[0]
		if b == 0x00 {
			// Flush console buffer before frame processing
			if len(consoleBuf) > 0 {
				d.broadcast(EventConsole, &ConsoleEvent{Text: string(consoleBuf)})
				consoleBuf = consoleBuf[:0]
			}
			if inFrame && len(frame) > 0 {
				// Decode and deliver directly to the pending waiter
				d.deliverFrame(frame)
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
	wantID := d.waiterID
	d.waiterMu.Unlock()

	if ch == nil {
		log.Printf("frame: no waiter, dropping %s (id=%d)", msgTypeName(header.MessageType), header.RequestID)
		return
	}

	if header.RequestID != wantID {
		log.Printf("frame: stale (got id=%d, want %d), dropping", header.RequestID, wantID)
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
	if d.port == nil {
		d.portMu.Unlock()
		return
	}
	d.port.Close()
	d.port = nil
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
		go d.serialReadLoop()
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
	if d.port != nil {
		d.port.Close()
		d.port = nil
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

// maxEvalSource is the maximum source bytes per EVAL_REQ frame.
const maxEvalSource = protocol.MaxPayload - 3

// chunkSource splits source into pieces that each fit in one EVAL_REQ.
// Splits only at top-level boundaries (after newlines where bracket and
// colon depth is zero), so multi-line forms are never broken across chunks.
func chunkSource(source string) []string {
	lines := strings.SplitAfter(source, "\n")
	var chunks []string
	var current strings.Builder
	depth := 0

	for _, line := range lines {
		if line == "" {
			continue
		}

		current.WriteString(line)

		for _, ch := range line {
			switch ch {
			case '[':
				depth++
			case ']', ';':
				if depth > 0 {
					depth--
				}
			case ':':
				depth++
			}
		}

		if depth == 0 && current.Len() > 0 {
			if current.Len() >= maxEvalSource || !strings.HasSuffix(line, "\n") {
				chunks = append(chunks, current.String())
				current.Reset()
			} else if current.Len() > maxEvalSource*3/4 {
				chunks = append(chunks, current.String())
				current.Reset()
			}
		}
	}
	if current.Len() > 0 {
		chunks = append(chunks, current.String())
	}
	return chunks
}

// sendFrame acquires writeMu, builds and writes a COBS frame.
func (d *Daemon) sendFrame(msgType byte, reqID uint16, payload []byte) error {
	wire, err := protocol.EncodeWireFrame(msgType, reqID, payload)
	if err != nil {
		return fmt.Errorf("build frame: %w", err)
	}

	d.writeMu.Lock()
	defer d.writeMu.Unlock()

	d.portMu.Lock()
	port := d.port
	d.portMu.Unlock()

	if port == nil {
		return ErrDisconnected
	}

	return port.Write(wire)
}

// registerWaiter sets up a per-request delivery channel. The serial
// reader will deliver matching frames here. Must be called before
// sending the frame. The caller holds reqMu so only one waiter exists.
func (d *Daemon) registerWaiter(reqID uint16) chan frameResponse {
	ch := make(chan frameResponse, 1)
	d.waiterMu.Lock()
	d.waiterID = reqID
	d.waiterCh = ch
	d.waiterMu.Unlock()
	return ch
}

// clearWaiter removes the registered waiter. Called after waitResponse
// returns, whether success or error.
func (d *Daemon) clearWaiter() {
	d.waiterMu.Lock()
	d.waiterID = 0
	d.waiterCh = nil
	d.waiterMu.Unlock()
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
	port := d.port
	d.portMu.Unlock()
	if port == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	chunks := chunkSource(source)
	var lastResult *EvalResult

	for _, chunk := range chunks {
		reqID := d.allocReqID()
		payload := protocol.BuildEvalPayload(chunk)

		ch := d.registerWaiter(reqID)
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
	port := d.port
	d.portMu.Unlock()
	if port == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	reqID := d.allocReqID()
	ch := d.registerWaiter(reqID)
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
	port := d.port
	d.portMu.Unlock()
	if port == nil {
		return nil, ErrDisconnected
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	reqID := d.allocReqID()
	ch := d.registerWaiter(reqID)
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
	port := d.port
	d.portMu.Unlock()

	if port == nil {
		return ErrDisconnected
	}

	return port.Write([]byte{0x03})
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
	case protocol.HelloRes:
		return "HELLO_RES"
	case protocol.EvalRes:
		return "EVAL_RES"
	case protocol.InfoRes:
		return "INFO_RES"
	case protocol.ResetRes:
		return "RESET_RES"
	case protocol.Error:
		return "ERROR"
	default:
		return fmt.Sprintf("0x%02x", t)
	}
}
