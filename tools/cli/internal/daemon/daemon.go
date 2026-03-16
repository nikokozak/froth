package daemon

import (
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"path/filepath"
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
	rpcTimeout        = 5 * time.Second
)

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

	// One FROTH-LINK/1 transaction at a time
	reqMu   sync.Mutex
	frameCh chan []byte

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
		portPath:   portPath,
		socketPath: filepath.Join(frothDir, "daemon.sock"),
		pidPath:    filepath.Join(frothDir, "daemon.pid"),
		frameCh:    make(chan []byte, 2),
		clients:    make(map[*rpcConn]struct{}),
		done:       make(chan struct{}),
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
	d.portMu.Unlock()

	log.Printf("device: %s on %s (%d-bit) at %s", hello.Version, hello.Board, hello.CellBits, port.Path())
	d.broadcast(EventConnected, &ConnectedEvent{
		Device: helloToResult(hello),
		Port:   port.Path(),
	})

	return nil
}

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
				frameCopy := make([]byte, len(frame))
				copy(frameCopy, frame)
				select {
				case d.frameCh <- frameCopy:
				default:
					// Drop if nobody waiting (stale response)
				}
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

func (d *Daemon) handleDisconnect(err error) {
	d.portMu.Lock()
	if d.port == nil {
		d.portMu.Unlock()
		return
	}
	d.port.Close()
	d.port = nil
	d.hello = nil
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

func (d *Daemon) broadcast(event string, params interface{}) {
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

// deviceEval sends an EVAL_REQ and returns the parsed response.
// Serialized: only one device transaction at a time.
func (d *Daemon) deviceEval(source string) (*EvalResult, error) {
	d.portMu.Lock()
	port := d.port
	d.portMu.Unlock()

	if port == nil {
		return nil, fmt.Errorf("device not connected")
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	drainFrames(d.frameCh)

	payload := protocol.BuildEvalPayload(source)
	wire, err := protocol.EncodeWireFrame(protocol.EvalReq, 1, payload)
	if err != nil {
		return nil, fmt.Errorf("build frame: %w", err)
	}

	if err := port.Write(wire); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	encoded, err := d.waitFrame(rpcTimeout)
	if err != nil {
		return nil, err
	}

	decoded, err := protocol.COBSDecode(encoded)
	if err != nil {
		return nil, fmt.Errorf("cobs decode: %w", err)
	}

	header, respPayload, err := protocol.ParseFrame(decoded)
	if err != nil {
		return nil, fmt.Errorf("parse frame: %w", err)
	}

	switch header.MessageType {
	case protocol.EvalRes:
		resp, err := protocol.ParseEvalResponse(respPayload)
		if err != nil {
			return nil, err
		}
		return &EvalResult{
			Status:    int(resp.Status),
			ErrorCode: int(resp.ErrorCode),
			FaultWord: resp.FaultWord,
			StackRepr: resp.StackRepr,
		}, nil
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

// deviceInfo sends an INFO_REQ and returns the parsed response.
func (d *Daemon) deviceInfo() (*InfoResult, error) {
	d.portMu.Lock()
	port := d.port
	d.portMu.Unlock()

	if port == nil {
		return nil, fmt.Errorf("device not connected")
	}

	d.reqMu.Lock()
	defer d.reqMu.Unlock()

	drainFrames(d.frameCh)

	wire, err := protocol.EncodeWireFrame(protocol.InfoReq, 1, nil)
	if err != nil {
		return nil, fmt.Errorf("build frame: %w", err)
	}

	if err := port.Write(wire); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	encoded, err := d.waitFrame(rpcTimeout)
	if err != nil {
		return nil, err
	}

	decoded, err := protocol.COBSDecode(encoded)
	if err != nil {
		return nil, fmt.Errorf("cobs decode: %w", err)
	}

	header, respPayload, err := protocol.ParseFrame(decoded)
	if err != nil {
		return nil, fmt.Errorf("parse frame: %w", err)
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

func (d *Daemon) waitFrame(timeout time.Duration) ([]byte, error) {
	select {
	case frame := <-d.frameCh:
		return frame, nil
	case <-time.After(timeout):
		return nil, fmt.Errorf("device response timeout")
	case <-d.done:
		return nil, fmt.Errorf("daemon shutting down")
	}
}

func drainFrames(ch chan []byte) {
	for {
		select {
		case <-ch:
		default:
			return
		}
	}
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
