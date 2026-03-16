package session

import (
	"fmt"
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
	// Two paths:
	//
	// A. portPath is specified:
	//    1. Open the serial port: serial.Open(portPath)
	//    2. Drain boot output: port.Drain(serial.DrainDuration)
	//    3. Send HELLO and get response: serial.ProbeHello(port, DefaultTimeout)
	//    4. Wrap in Session and return.
	//
	// B. portPath is empty (auto-detect):
	//    1. Call serial.Discover() which handles enumeration + probing.
	//    2. Wrap the returned port + hello in Session and return.

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

// Eval sends Froth source for evaluation and returns the result.
func (s *Session) Eval(source string) (*protocol.EvalResponse, error) {
	// Steps:
	//
	// 1. Get a request ID: atomic increment of nextReqID.
	//    Cap at 0xFFFE (0xFFFF is reserved sentinel).
	//
	// 2. Build EVAL_REQ payload:
	//    payload := protocol.BuildEvalPayload(source)
	//
	// 3. Build wire frame:
	//    wire, err := protocol.EncodeWireFrame(protocol.EvalReq, reqID, payload)
	//
	// 4. Write to port:
	//    s.port.Write(wire)
	//
	// 5. Read response frame:
	//    encoded, err := s.port.ReadFrame(DefaultTimeout)
	//
	// 6. COBS-decode:
	//    decoded, err := protocol.COBSDecode(encoded)
	//
	// 7. Parse frame:
	//    header, payload, err := protocol.ParseFrame(decoded)
	//
	// 8. Check header.MessageType:
	//    - EvalRes: parse and return protocol.ParseEvalResponse(payload)
	//    - Error:   parse protocol.ParseErrorResponse(payload),
	//               return as wrapped error
	//    - other:   return error "unexpected response type"
	//
	// 9. Verify header.RequestID == reqID.
	//    Mismatch is a protocol error (should not happen with stop-and-wait).

	reqID := uint16(atomic.AddUint32(&s.nextReqID, 1) % 0xFFFF)

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

// allocReqID returns the next request ID, wrapping at 0xFFFE.
func (s *Session) allocReqID() uint16 {
	id := atomic.AddUint32(&s.nextReqID, 1)
	return uint16(id % 0xFFFE)
}
