package session

import (
	"errors"
	"fmt"
	"log"
	"sync"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
	"github.com/nikokozak/froth/tools/cli/internal/serial"
)

// CommandTimeout is the safety timeout for non-eval operations. These
// should respond quickly; a long timeout catches transport wedges.
const CommandTimeout = 10 * time.Second

const (
	attachTimeout     = 5 * time.Second
	maxAttachRetries  = 3
	attachRetryDelay  = 500 * time.Millisecond
	keepaliveInterval = 2 * time.Second
	detachTimeout     = 5 * time.Second
)

// Session holds a live connection to a Froth device.
type Session struct {
	transport serial.Transport
	hello     *protocol.HelloResponse

	attached  bool
	sessionID uint64
	nextSeq   uint16
	activeSeq uint16

	writeMu sync.Mutex

	keepaliveTicker *time.Ticker
	keepaliveStop   chan struct{}
	keepaliveDone   chan struct{}

	OutputHandler func([]byte)
}

// Connect opens a session to a Froth device and performs HELLO first.
func Connect(portPath string) (*Session, error) {
	if portPath != "" {
		port, hello, err := serial.OpenAndProbe(portPath)
		if err != nil {
			return nil, fmt.Errorf("connect %s: %w", portPath, err)
		}
		return &Session{transport: port, hello: hello}, nil
	}

	port, hello, err := serial.Discover()
	if err != nil {
		return nil, err
	}
	return &Session{transport: port, hello: hello}, nil
}

// Close detaches best-effort, then closes the transport.
func (s *Session) Close() error {
	if s.attached {
		_ = s.detach()
	}
	return s.transport.Close()
}

// Abort closes the transport immediately without sending DETACH. Use
// this when the session is no longer in a clean state for protocol
// shutdown, for example if the host is aborting an in-flight eval.
func (s *Session) Abort() error {
	s.enterDirectMode()
	return s.transport.Close()
}

// DeviceInfo returns the cached HELLO_RES data.
func (s *Session) DeviceInfo() *protocol.HelloResponse {
	return s.hello
}

func (s *Session) enterDirectMode() {
	s.stopKeepalive()
	s.attached = false
	s.sessionID = 0
	s.nextSeq = 0
	s.activeSeq = 0
}

func (s *Session) startKeepalive() {
	s.stopKeepalive()
	s.keepaliveTicker = time.NewTicker(keepaliveInterval)
	s.keepaliveStop = make(chan struct{})
	s.keepaliveDone = make(chan struct{})
	go s.keepaliveLoop(s.sessionID, s.keepaliveTicker, s.keepaliveStop, s.keepaliveDone)
}

func (s *Session) stopKeepalive() {
	if s.keepaliveTicker == nil {
		return
	}

	s.keepaliveTicker.Stop()
	if s.keepaliveStop != nil {
		select {
		case <-s.keepaliveStop:
		default:
			close(s.keepaliveStop)
		}
	}
	if s.keepaliveDone != nil {
		<-s.keepaliveDone
	}
	s.keepaliveTicker = nil
	s.keepaliveStop = nil
	s.keepaliveDone = nil
}

func (s *Session) keepaliveLoop(sessionID uint64, ticker *time.Ticker, stop <-chan struct{},
	done chan struct{}) {
	if ticker == nil || stop == nil {
		if done != nil {
			close(done)
		}
		return
	}
	defer close(done)

	for {
		select {
		case <-ticker.C:
			wire, err := protocol.EncodeWireFrame(sessionID, protocol.Keepalive, 0, nil)
			if err != nil {
				continue
			}
			s.writeMu.Lock()
			_ = s.transport.Write(wire)
			s.writeMu.Unlock()
		case <-stop:
			return
		}
	}
}

func (s *Session) allocSeq() uint16 {
	seq := s.nextSeq
	s.nextSeq++
	if s.nextSeq == 0 {
		s.nextSeq = 1
	}
	return seq
}

func (s *Session) sendFrame(msgType byte, seq uint16, payload []byte) error {
	wire, err := protocol.EncodeWireFrame(s.sessionID, msgType, seq, payload)
	if err != nil {
		return fmt.Errorf("build frame: %w", err)
	}

	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	return s.transport.Write(wire)
}

func (s *Session) attach() error {
	if s.attached {
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

		wire, err := protocol.EncodeWireFrame(sessionID, protocol.AttachReq, 0, nil)
		if err != nil {
			return err
		}

		s.writeMu.Lock()
		writeErr := s.transport.Write(wire)
		s.writeMu.Unlock()
		if writeErr != nil {
			return fmt.Errorf("attach write: %w", writeErr)
		}

		deadline := time.Now().Add(attachTimeout)

		var payload []byte
		for {
			remaining := time.Until(deadline)
			if remaining <= 0 {
				return fmt.Errorf("attach: device response timeout")
			}

			encoded, err := serial.ReadFrameTransport(s.transport, remaining, nil)
			if err != nil {
				if errors.Is(err, serial.ErrTimeout) {
					return fmt.Errorf("attach: device response timeout")
				}
				return fmt.Errorf("attach read: %w", err)
			}

			decoded, err := protocol.COBSDecode(encoded)
			if err != nil {
				log.Printf("attach: corrupt COBS, discarding")
				continue
			}

			header, respPayload, err := protocol.ParseFrame(decoded)
			if err != nil {
				log.Printf("attach: bad frame, discarding")
				continue
			}
			if header.MessageType != protocol.AttachRes {
				log.Printf("attach: discard unexpected response type 0x%02x", header.MessageType)
				continue
			}
			if header.SessionID != sessionID {
				log.Printf("attach: discard stale session response (%016x != %016x)", header.SessionID, sessionID)
				continue
			}
			if header.Seq != 0 {
				log.Printf("attach: discard unexpected seq %d", header.Seq)
				continue
			}

			payload = respPayload
			break
		}

		status, err := protocol.ParseAttachResponse(payload)
		if err != nil {
			return fmt.Errorf("attach parse: %w", err)
		}

		switch status {
		case protocol.AttachStatusOK:
			s.attached = true
			s.sessionID = sessionID
			s.nextSeq = 1
			s.startKeepalive()
			return nil
		case protocol.AttachStatusBusy:
			continue
		default:
			return fmt.Errorf("attach rejected: status %d", status)
		}
	}

	return fmt.Errorf("attach: device busy after %d retries", maxAttachRetries+1)
}

func (s *Session) detach() error {
	if !s.attached {
		return nil
	}

	s.stopKeepalive()

	// DETACH uses the current seq without advancing.
	seq := s.nextSeq
	wire, err := protocol.EncodeWireFrame(s.sessionID, protocol.DetachReq, seq, nil)
	if err != nil {
		s.enterDirectMode()
		return err
	}

	s.writeMu.Lock()
	writeErr := s.transport.Write(wire)
	s.writeMu.Unlock()
	if writeErr != nil {
		s.enterDirectMode()
		return writeErr
	}

	deadline := time.Now().Add(detachTimeout)
	for {
		remaining := time.Until(deadline)
		if remaining <= 0 {
			break
		}

		encoded, err := serial.ReadFrameTransport(s.transport, remaining, nil)
		if err != nil {
			if errors.Is(err, serial.ErrTimeout) {
				break
			}
			break
		}

		decoded, decErr := protocol.COBSDecode(encoded)
		if decErr != nil {
			log.Printf("detach: corrupt COBS, discarding")
			continue
		}

		header, _, parseErr := protocol.ParseFrame(decoded)
		if parseErr != nil {
			log.Printf("detach: bad frame, discarding")
			continue
		}
		if header.MessageType != protocol.DetachRes {
			log.Printf("detach: discard unexpected type 0x%02x", header.MessageType)
			continue
		}
		if header.SessionID != s.sessionID {
			log.Printf("detach: discard stale session response (%016x != %016x)", header.SessionID, s.sessionID)
			continue
		}
		if header.Seq != seq {
			log.Printf("detach: discard unexpected seq %d", header.Seq)
			continue
		}
		break
	}

	s.enterDirectMode()
	return nil
}

// waitValidResponse reads frames until the terminal response arrives.
// OUTPUT_DATA is streamed to OutputHandler. INPUT_WAIT is logged and ignored.
func (s *Session) waitValidResponse(seq uint16, timeout time.Duration) (*protocol.Header, []byte, error) {
	noTimeout := timeout == 0
	var deadline time.Time
	if !noTimeout {
		deadline = time.Now().Add(timeout)
	}

	for {
		var readTimeout time.Duration
		if noTimeout {
			readTimeout = 30 * time.Second
		} else {
			readTimeout = time.Until(deadline)
			if readTimeout <= 0 {
				return nil, nil, fmt.Errorf("device response timeout")
			}
		}

		encoded, err := serial.ReadFrameTransport(s.transport, readTimeout, nil)
		if err != nil {
			if errors.Is(err, serial.ErrTimeout) {
				if noTimeout {
					continue
				}
				return nil, nil, fmt.Errorf("device response timeout")
			}
			return nil, nil, fmt.Errorf("read: %w", err)
		}

		decoded, err := protocol.COBSDecode(encoded)
		if err != nil {
			log.Printf("frame: corrupt COBS, discarding")
			continue
		}

		header, payload, err := protocol.ParseFrame(decoded)
		if err != nil {
			log.Printf("frame: bad frame, discarding")
			continue
		}

		if header.SessionID != s.sessionID {
			continue
		}

		switch header.MessageType {
		case protocol.OutputData:
			if s.activeSeq == 0 || header.Seq != s.activeSeq {
				continue
			}
			if s.OutputHandler != nil {
				data, parseErr := protocol.ParseOutputData(payload)
				if parseErr == nil {
					s.OutputHandler(data)
				}
			}
			continue
		case protocol.InputWait:
			if s.activeSeq == 0 || header.Seq != s.activeSeq {
				continue
			}
			return nil, nil, fmt.Errorf("device waiting for input (interactive input unsupported in serial mode)")
		default:
			if header.Seq != seq {
				log.Printf("frame: seq mismatch (got %d, want %d)", header.Seq, seq)
				continue
			}
			return header, payload, nil
		}
	}
}

// Reset sends RESET_REQ.
func (s *Session) Reset() (*protocol.ResetResponse, error) {
	if err := s.attach(); err != nil {
		return nil, fmt.Errorf("reset: %w", err)
	}

	seq := s.allocSeq()
	if err := s.sendFrame(protocol.ResetReq, seq, nil); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	header, respPayload, err := s.waitValidResponse(seq, CommandTimeout)
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

// Eval sends Froth source for evaluation. Long source is chunked first.
func (s *Session) Eval(source string) (*protocol.EvalResponse, error) {
	if err := s.attach(); err != nil {
		return nil, fmt.Errorf("eval: %w", err)
	}

	chunks, err := ChunkEvalSource(source)
	if err != nil {
		return nil, err
	}

	var lastResp *protocol.EvalResponse

	for _, chunk := range chunks {
		seq := s.allocSeq()
		s.activeSeq = seq
		payload := protocol.BuildEvalPayload(chunk)

		if err := s.sendFrame(protocol.EvalReq, seq, payload); err != nil {
			s.activeSeq = 0
			return nil, fmt.Errorf("write: %w", err)
		}

		header, respPayload, err := s.waitValidResponse(seq, 0)
		s.activeSeq = 0
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
			errResp, err := protocol.ParseErrorResponse(respPayload)
			if err != nil {
				return nil, err
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
