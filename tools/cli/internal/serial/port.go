package serial

import (
	"errors"
	"fmt"
	"time"

	"go.bug.st/serial"
)

var ErrTimeout = errors.New("serial read timeout")

// Port wraps a serial connection with byte-level and frame-level I/O.
type Port struct {
	port serial.Port
	path string
}

// Open opens a serial port at the given path with Froth defaults (115200 8N1).
func Open(path string) (*Port, error) {
	conn, err := serial.Open(path, &serial.Mode{
		BaudRate: 115200,
		DataBits: 8,
		StopBits: serial.OneStopBit,
		Parity:   serial.NoParity,
	})
	if err != nil {
		return nil, fmt.Errorf("open serial port: %w", err)
	}
	return &Port{port: conn, path: path}, nil
}

// Close closes the serial port.
func (p *Port) Close() error {
	return p.port.Close()
}

// Write sends raw bytes to the serial port.
func (p *Port) Write(data []byte) error {
	n, err := p.port.Write(data)
	if err != nil {
		return fmt.Errorf("serial write: %w", err)
	}
	if n != len(data) {
		return fmt.Errorf("short write: wrote %d of %d bytes", n, len(data))
	}
	return nil
}

// ReadFrame reads bytes until a complete COBS frame is captured (bytes
// between two 0x00 delimiters). Non-frame bytes are discarded.
// Returns the raw COBS-encoded bytes (without delimiters).
func (p *Port) ReadFrame(timeout time.Duration) ([]byte, error) {
	deadline := time.Now().Add(timeout)
	buf := make([]byte, 1)
	var frame []byte
	inFrame := false

	for {
		remaining := time.Until(deadline)
		if remaining <= 0 {
			return nil, ErrTimeout
		}
		p.port.SetReadTimeout(remaining)

		n, err := p.port.Read(buf)
		if err != nil {
			return nil, fmt.Errorf("serial read: %w", err)
		}
		if n == 0 {
			return nil, ErrTimeout
		}

		b := buf[0]

		if b == 0x00 {
			if inFrame && len(frame) > 0 {
				return frame, nil
			}
			// Start (or restart) a new frame.
			frame = frame[:0]
			inFrame = true
			continue
		}

		if inFrame {
			frame = append(frame, b)
		}
		// Bytes outside a frame (before the first 0x00) are discarded.
	}
}

// Drain reads and discards all bytes for the given duration.
// Clears boot messages before sending the first frame.
func (p *Port) Drain(duration time.Duration) {
	deadline := time.Now().Add(duration)
	buf := make([]byte, 256)
	for time.Now().Before(deadline) {
		remaining := time.Until(deadline)
		p.port.SetReadTimeout(remaining)
		p.port.Read(buf)
	}
}
