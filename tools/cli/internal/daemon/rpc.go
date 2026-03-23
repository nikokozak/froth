package daemon

import (
	"bufio"
	"encoding/json"
	"net"
	"os"
	"sync"
)

// JSON-RPC 2.0 types

type rpcRequest struct {
	JSONRPC string          `json:"jsonrpc"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
	ID      interface{}     `json:"id"`
}

type rpcResponse struct {
	JSONRPC string      `json:"jsonrpc"`
	Result  interface{} `json:"result,omitempty"`
	Error   *RPCError   `json:"error,omitempty"`
	ID      interface{} `json:"id"`
}

type rpcNotification struct {
	JSONRPC string      `json:"jsonrpc"`
	Method  string      `json:"method"`
	Params  interface{} `json:"params,omitempty"`
}

// RPCError is a JSON-RPC 2.0 error object.
type RPCError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

const (
	errParseError     = -32700
	errInvalidRequest = -32600
	errMethodNotFound = -32601
	errDeviceError    = -32000
	errNotConnected   = -32001
)

// Event type constants
const (
	EventConsole      = "console"
	EventConnected    = "connected"
	EventDisconnected = "disconnected"
	EventReconnecting = "reconnecting"
	EventInputWait    = "input_wait"
)

// Domain types shared between server and client

type EvalParams struct {
	Source string `json:"source"`
}

type EvalResult struct {
	Status    int    `json:"status"`
	ErrorCode int    `json:"error_code,omitempty"`
	FaultWord string `json:"fault_word,omitempty"`
	StackRepr string `json:"stack_repr,omitempty"`
}

type HelloResult struct {
	CellBits   int    `json:"cell_bits"`
	MaxPayload int    `json:"max_payload"`
	HeapSize   int    `json:"heap_size"`
	HeapUsed   int    `json:"heap_used"`
	SlotCount  int    `json:"slot_count"`
	Version    string `json:"version"`
	Board      string `json:"board"`
}

type InfoResult struct {
	HeapSize         int    `json:"heap_size"`
	HeapUsed         int    `json:"heap_used"`
	HeapOverlayUsed  int    `json:"heap_overlay_used"`
	SlotCount        int    `json:"slot_count"`
	SlotOverlayCount int    `json:"slot_overlay_count"`
	Version          string `json:"version"`
}

type ResetResult struct {
	Status           int    `json:"status"`
	HeapSize         int    `json:"heap_size"`
	HeapUsed         int    `json:"heap_used"`
	HeapOverlayUsed  int    `json:"heap_overlay_used"`
	SlotCount        int    `json:"slot_count"`
	SlotOverlayCount int    `json:"slot_overlay_count"`
	Version          string `json:"version"`
}

type StatusResult struct {
	PID           int          `json:"pid"`
	APIVersion    int          `json:"api_version"`
	DaemonVersion string       `json:"daemon_version"`
	Running       bool         `json:"running"`
	Connected     bool         `json:"connected"`
	Reconnecting  bool         `json:"reconnecting"`
	Target        string       `json:"target"`
	Device        *HelloResult `json:"device,omitempty"`
	Port          string       `json:"port,omitempty"`
}

type ConsoleEvent struct {
	Data []byte `json:"data"`
}

type InputWaitEvent struct {
	Reason int `json:"reason"`
	Seq    int `json:"seq"`
}

type InputParams struct {
	Data []byte `json:"data"`
	Seq  int    `json:"seq"`
}

type ConnectedEvent struct {
	Device HelloResult `json:"device"`
	Port   string      `json:"port"`
}

// rpcConn is a server-side per-client connection handler.
type rpcConn struct {
	nc             net.Conn
	daemon         *Daemon
	scanner        *bufio.Scanner
	enc            *json.Encoder
	mu             sync.Mutex
	notifyCh       chan *rpcNotification // buffered channel for async notifications
	done           chan struct{}
	closeOnce      sync.Once
	stateMu        sync.Mutex
	droppedConsole bool
}

func newRPCConn(nc net.Conn, d *Daemon) *rpcConn {
	c := &rpcConn{
		nc:       nc,
		daemon:   d,
		scanner:  bufio.NewScanner(nc),
		enc:      json.NewEncoder(nc),
		notifyCh: make(chan *rpcNotification, 64),
		done:     make(chan struct{}),
	}
	go c.notifyLoop()
	return c
}

// notifyLoop drains the notification channel and writes to the socket.
// Runs in its own goroutine so broadcast never blocks the serial read loop.
func (c *rpcConn) notifyLoop() {
	for {
		select {
		case <-c.done:
			return
		case n := <-c.notifyCh:
			if n == nil {
				continue
			}
			c.mu.Lock()
			c.enc.Encode(n)
			c.mu.Unlock()
		}
	}
}

func (c *rpcConn) serve() {
	defer c.close()

	for c.scanner.Scan() {
		line := c.scanner.Bytes()

		var req rpcRequest
		if err := json.Unmarshal(line, &req); err != nil {
			c.sendError(nil, errParseError, "parse error")
			continue
		}

		if req.JSONRPC != "2.0" {
			c.sendError(req.ID, errInvalidRequest, "invalid jsonrpc version")
			continue
		}

		c.handleRequest(&req)
	}
}

func (c *rpcConn) handleRequest(req *rpcRequest) {
	switch req.Method {
	// Interrupt and input bypass reqMu so they can run during eval.
	case "interrupt":
		go c.handleInterrupt(req)
	case "input":
		go c.handleInput(req)
	case "hello":
		c.handleHello(req)
	case "eval":
		go c.handleEval(req)
	case "info":
		c.handleInfo(req)
	case "status":
		c.handleStatus(req)
	case "reset":
		c.handleReset(req)
	default:
		c.sendError(req.ID, errMethodNotFound, "unknown method: "+req.Method)
	}
}

func (c *rpcConn) handleHello(req *rpcRequest) {
	c.daemon.portMu.Lock()
	hello := c.daemon.hello
	c.daemon.portMu.Unlock()

	if hello == nil {
		c.sendError(req.ID, errNotConnected, "device not connected")
		return
	}

	c.sendResult(req.ID, helloToResult(hello))
}

func (c *rpcConn) handleEval(req *rpcRequest) {
	var params EvalParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		c.sendError(req.ID, errInvalidRequest, "invalid params")
		return
	}

	result, err := c.daemon.deviceEval(params.Source, c)
	if err != nil {
		c.sendError(req.ID, errDeviceError, err.Error())
		return
	}

	c.sendResult(req.ID, result)
}

func (c *rpcConn) handleInfo(req *rpcRequest) {
	result, err := c.daemon.deviceInfo()
	if err != nil {
		c.sendError(req.ID, errDeviceError, err.Error())
		return
	}

	c.sendResult(req.ID, result)
}

func (c *rpcConn) handleReset(req *rpcRequest) {
	result, err := c.daemon.deviceReset()
	if err != nil {
		c.sendError(req.ID, errDeviceError, err.Error())
		return
	}

	c.sendResult(req.ID, result)
}

func (c *rpcConn) handleInterrupt(req *rpcRequest) {
	err := c.daemon.deviceInterrupt()
	if err != nil {
		c.sendError(req.ID, errDeviceError, err.Error())
		return
	}
	c.sendResult(req.ID, struct{}{})
}

func (c *rpcConn) handleInput(req *rpcRequest) {
	var params InputParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		c.sendError(req.ID, errInvalidRequest, "invalid params")
		return
	}
	if params.Seq <= 0 || params.Seq > 0xFFFF {
		c.sendError(req.ID, errInvalidRequest, "invalid input seq")
		return
	}

	if err := c.daemon.deviceSendInput(uint16(params.Seq), params.Data); err != nil {
		c.sendError(req.ID, errDeviceError, err.Error())
		return
	}

	c.sendResult(req.ID, struct{}{})
}

func (c *rpcConn) handleStatus(req *rpcRequest) {
	c.daemon.portMu.Lock()
	connected := c.daemon.conn != nil
	hello := c.daemon.hello
	portPath := c.daemon.portPath
	reconnecting := c.daemon.reconnecting.Load()
	target := "serial"
	if c.daemon.local {
		target = "local"
	}
	c.daemon.portMu.Unlock()

	result := &StatusResult{
		PID:           os.Getpid(),
		APIVersion:    APIVersion,
		DaemonVersion: DaemonVersion,
		Running:       true,
		Connected:     connected,
		Reconnecting:  reconnecting,
		Target:        target,
		Port:          portPath,
	}
	if hello != nil {
		hr := helloToResult(hello)
		result.Device = &hr
	}

	c.sendResult(req.ID, result)
}

func (c *rpcConn) sendResult(id interface{}, result interface{}) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.enc.Encode(&rpcResponse{
		JSONRPC: "2.0",
		Result:  result,
		ID:      id,
	})
}

func (c *rpcConn) sendError(id interface{}, code int, msg string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.enc.Encode(&rpcResponse{
		JSONRPC: "2.0",
		Error:   &RPCError{Code: code, Message: msg},
		ID:      id,
	})
}

func (c *rpcConn) sendNotification(method string, params interface{}) {
	select {
	case <-c.done:
		return
	default:
	}

	if method == EventConsole {
		if params = c.decorateConsoleEvent(params); params == nil {
			return
		}
	}

	n := &rpcNotification{
		JSONRPC: "2.0",
		Method:  method,
		Params:  params,
	}
	select {
	case <-c.done:
		return
	case c.notifyCh <- n:
		return
	default:
	}

	if method == EventInputWait {
		select {
		case <-c.done:
		case c.notifyCh <- n:
		}
		return
	}

	select {
	case <-c.done:
		return
	case c.notifyCh <- n:
	default:
		// Client too slow, drop notification rather than block serial read loop
		if method == EventConsole {
			c.stateMu.Lock()
			c.droppedConsole = true
			c.stateMu.Unlock()
		}
	}
}

func (c *rpcConn) close() {
	c.closeOnce.Do(func() {
		close(c.done)
		c.nc.Close()
	})
}

func (c *rpcConn) decorateConsoleEvent(params interface{}) interface{} {
	evt, ok := params.(*ConsoleEvent)
	if !ok || evt == nil {
		return params
	}

	c.stateMu.Lock()
	dropped := c.droppedConsole
	c.droppedConsole = false
	c.stateMu.Unlock()

	if !dropped {
		return params
	}

	copyEvt := *evt
	prefix := []byte("[froth] console output dropped\n")
	copyEvt.Data = append(append([]byte(nil), prefix...), copyEvt.Data...)
	return &copyEvt
}
