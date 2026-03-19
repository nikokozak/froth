package session

import (
	"errors"
	"fmt"
	"io"
	"log"
	"strings"
	"sync/atomic"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
	"github.com/nikokozak/froth/tools/cli/internal/serial"
)

// CommandTimeout is the safety timeout for non-eval operations (info,
// reset, hello). These should respond within milliseconds. 10s catches
// transport failures without interfering with normal operation.
const CommandTimeout = 10 * time.Second

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

		hello, err := serial.ProbeHello(port)
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

// waitValidResponse reads frames from serial until one decodes
// successfully and has a matching request ID.
// If timeout > 0, gives up after that duration (for info/reset).
// If timeout == 0, waits indefinitely (for eval).
// Corrupt frames and ID mismatches are discarded (up to 3 retries
// for timed operations, unlimited for no-timeout).
func (s *Session) waitValidResponse(reqID uint16, timeout time.Duration) (*protocol.Header, []byte, error) {
	noTimeout := timeout == 0
	var deadline time.Time
	if !noTimeout {
		deadline = time.Now().Add(timeout)
	}
	maxRetries := 3
	attempt := 0
	var lastErr error

	for {
		if !noTimeout {
			remaining := time.Until(deadline)
			if remaining <= 0 {
				break
			}
			encoded, err := s.port.ReadFrame(remaining)
			if err != nil {
				return nil, nil, fmt.Errorf("read response: %w", err)
			}
			if header, payload, done := s.tryDecode(encoded, reqID, &lastErr); done {
				return header, payload, nil
			}
		} else {
			// No timeout: use a long read window, retry on timeout
			encoded, err := s.port.ReadFrame(30 * time.Second)
			if err != nil {
				if errors.Is(err, serial.ErrTimeout) {
					continue // Keep waiting
				}
				return nil, nil, fmt.Errorf("read response: %w", err)
			}
			if header, payload, done := s.tryDecode(encoded, reqID, &lastErr); done {
				return header, payload, nil
			}
		}

		attempt++
		if !noTimeout && attempt > maxRetries {
			break
		}
	}

	if lastErr != nil {
		return nil, nil, fmt.Errorf("frame error after retries: %w", lastErr)
	}
	return nil, nil, fmt.Errorf("device response timeout")
}

// tryDecode attempts COBS decode, frame parse, and ID match.
// Returns (header, payload, true) on success, or logs and returns
// (nil, nil, false) on failure (caller should retry).
func (s *Session) tryDecode(encoded []byte, reqID uint16, lastErr *error) (*protocol.Header, []byte, bool) {
	decoded, err := protocol.COBSDecode(encoded)
	if err != nil {
		*lastErr = fmt.Errorf("cobs decode: %w", err)
		log.Printf("frame: discard corrupt COBS (%v)", err)
		return nil, nil, false
	}

	header, payload, err := protocol.ParseFrame(decoded)
	if err != nil {
		*lastErr = fmt.Errorf("parse frame: %w", err)
		log.Printf("frame: discard bad frame (%v)", err)
		return nil, nil, false
	}

	if header.RequestID != reqID {
		*lastErr = fmt.Errorf("stale response (got ID %d, want %d)", header.RequestID, reqID)
		log.Printf("frame: discard stale (got ID %d, want %d)", header.RequestID, reqID)
		return nil, nil, false
	}

	return header, payload, true
}

// Reset sends a RESET_REQ, which resets the device state to a "bare" firmware boot (no user code).
func (s *Session) Reset() (*protocol.ResetResponse, error) {
	reqID := s.allocReqID()

	wire, err := protocol.EncodeWireFrame(protocol.ResetReq, reqID, nil)
	if err != nil {
		return nil, fmt.Errorf("build frame: %w", err)
	}

	if err := s.port.Write(wire); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	header, respPayload, err := s.waitValidResponse(reqID, CommandTimeout)
	if err != nil {
		return nil, err
	}

	switch header.MessageType {
	case protocol.ResetRes:
		return protocol.ParseResetResponse(respPayload)
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

// maxEvalSource is the maximum source bytes per EVAL_REQ frame.
const maxEvalSource = protocol.MaxPayload - 3

// chunkSource splits source into pieces that fit in one EVAL_REQ.
// Splits only at top-level boundaries (depth == 0 after tracking
// brackets and colon-semicolon nesting).
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

// Eval sends Froth source for evaluation and returns the result.
// Source longer than 253 bytes is automatically chunked on line boundaries.
func (s *Session) Eval(source string) (*protocol.EvalResponse, error) {
	chunks := chunkSource(source)
	var lastResp *protocol.EvalResponse

	for _, chunk := range chunks {
		reqID := s.allocReqID()

		payload := protocol.BuildEvalPayload(chunk)

		wire, err := protocol.EncodeWireFrame(protocol.EvalReq, reqID, payload)
		if err != nil {
			return nil, fmt.Errorf("build frame: %w", err)
		}

		if err := s.port.Write(wire); err != nil {
			return nil, fmt.Errorf("write: %w", err)
		}

		header, respPayload, err := s.waitValidResponse(reqID, 0) // no timeout: eval can run forever
		if err != nil {
			return nil, err
		}

		switch header.MessageType {
		case protocol.EvalRes:
			resp, err := protocol.ParseEvalResponse(respPayload)
			if err != nil {
				return nil, err
			}
			lastResp = resp
			if resp.Status != 0 {
				return resp, nil
			}
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

	if lastResp == nil {
		return &protocol.EvalResponse{Status: 0, StackRepr: "[]"}, nil
	}
	return lastResp, nil
}

// allocReqID returns the next request ID in [1, 0xFFFE].
// 0x0000 is unused, 0xFFFF is the sentinel (ReqIDNone).
func (s *Session) allocReqID() uint16 {
	id := atomic.AddUint32(&s.nextReqID, 1)
	return uint16((id % 0xFFFE) + 1)
}
