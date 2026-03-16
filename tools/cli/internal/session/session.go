package session

import (
	"fmt"
	"io"
	"sync/atomic"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
	"github.com/nikokozak/froth/tools/cli/internal/serial"
)

// Default timeout for request/response round-trips.
const DefaultTimeout = 5 * time.Second

// Session holds a live connection to a Froth device.
// It owns the serial port and tracks the HELLO handshake result.
type Session struct {
	port      *serial.Port
	hello     *protocol.HelloResponse
	nextReqID uint32 // atomic counter for request IDs
}

// Connect opens a session to a Froth device. If portPath is empty,
// auto-discovers by probing available serial ports. If portPath is
// specified, opens that port directly.
//
// Always performs a HELLO handshake before returning.
func Connect(portPath string) (*Session, error) {
	if portPath != "" {
		port, err := serial.Open(portPath)
		if err != nil {
			return nil, fmt.Errorf("open %s: %w", portPath, err)
		}

		port.Drain(serial.DrainDuration)

		hello, err := serial.ProbeHello(port, DefaultTimeout)
		if err != nil {
			port.Close()
			return nil, fmt.Errorf("handshake on %s: %w", portPath, err)
		}

		return &Session{port: port, hello: hello, nextReqID: 1}, nil
	}

	port, hello, err := serial.Discover()
	if err != nil {
		return nil, err
	}
	return &Session{port: port, hello: hello, nextReqID: 1}, nil
}

// Close shuts down the serial connection.
func (s *Session) Close() error {
	return s.port.Close()
}

// DeviceInfo returns the HELLO_RES data from the handshake.
func (s *Session) DeviceInfo() *protocol.HelloResponse {
	return s.hello
}

// SetPassthrough routes non-frame device output to w.
func (s *Session) SetPassthrough(w io.Writer) {
	s.port.PassthroughWriter = w
}

// Eval sends Froth source for evaluation and returns the result.
func (s *Session) Eval(source string) (*protocol.EvalResponse, error) {
	reqID := s.allocReqID()

	payload := protocol.BuildEvalPayload(source)

	wire, err := protocol.EncodeWireFrame(protocol.EvalReq, reqID, payload)
	if err != nil {
		return nil, fmt.Errorf("build frame: %w", err)
	}

	if err := s.port.Write(wire); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	encoded, err := s.port.ReadFrame(DefaultTimeout)
	if err != nil {
		return nil, fmt.Errorf("read response: %w", err)
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
		return protocol.ParseEvalResponse(respPayload)
	case protocol.Error:
		errResp, parseErr := protocol.ParseErrorResponse(respPayload)
		if parseErr != nil {
			return nil, parseErr
		}
		return nil, fmt.Errorf("device error (cat %d): %s", errResp.Category, errResp.Detail)
	default:
		return nil, fmt.Errorf("unexpected response type: 0x%02x", header.MessageType)
	}
}

// allocReqID returns the next request ID in [1, 0xFFFE].
// 0x0000 is unused, 0xFFFF is the sentinel (ReqIDNone).
func (s *Session) allocReqID() uint16 {
	id := atomic.AddUint32(&s.nextReqID, 1)
	return uint16((id % 0xFFFE) + 1)
}
