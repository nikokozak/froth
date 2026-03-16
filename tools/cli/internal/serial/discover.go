package serial

import (
	"fmt"
	"regexp"
	"time"

	"go.bug.st/serial"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
)

const DefaultProbeTimeout = 500 * time.Millisecond

// DrainDuration exceeds the device's 750ms safe-boot window.
const DrainDuration = 1200 * time.Millisecond

// candidatePattern matches likely USB-serial ports on macOS and Linux.
var candidatePattern = regexp.MustCompile(
	`^/dev/(tty|cu)\.(usbserial|usbmodem|SLAB_USBtoUART|USB|ACM)` +
		`|^/dev/tty(USB|ACM)`,
)

// IsCandidate reports whether a port path matches the USB-serial pattern.
func IsCandidate(path string) bool {
	return candidatePattern.MatchString(path)
}

// ListCandidates returns all port paths matching the USB-serial pattern.
func ListCandidates() ([]string, error) {
	ports, err := serial.GetPortsList()
	if err != nil {
		return nil, err
	}
	var result []string
	for _, p := range ports {
		if candidatePattern.MatchString(p) {
			result = append(result, p)
		}
	}
	return result, nil
}

// Discover probes available serial ports for a Froth device.
// Returns the first port that responds to HELLO_REQ.
func Discover() (*Port, *protocol.HelloResponse, error) {
	ports, err := serial.GetPortsList()
	if err != nil {
		return nil, nil, fmt.Errorf("enumerate serial ports: %w", err)
	}

	for _, path := range ports {
		if !candidatePattern.MatchString(path) {
			continue
		}

		conn, err := Open(path)
		if err != nil {
			continue
		}

		conn.Drain(DrainDuration)

		resp, err := ProbeHello(conn, DefaultProbeTimeout)
		if err == nil {
			return conn, resp, nil
		}
		conn.Close()
	}

	return nil, nil, fmt.Errorf("no Froth device found")
}

// ProbeHello sends a HELLO_REQ and waits for a HELLO_RES.
func ProbeHello(port *Port, timeout time.Duration) (*protocol.HelloResponse, error) {
	frame, err := protocol.EncodeWireFrame(protocol.HelloReq, 0, nil)
	if err != nil {
		return nil, fmt.Errorf("build HELLO_REQ: %w", err)
	}

	if err := port.Write(frame); err != nil {
		return nil, err
	}

	encoded, err := port.ReadFrame(timeout)
	if err != nil {
		return nil, err
	}

	decoded, err := protocol.COBSDecode(encoded)
	if err != nil {
		return nil, fmt.Errorf("COBS decode: %w", err)
	}

	header, payload, err := protocol.ParseFrame(decoded)
	if err != nil {
		return nil, fmt.Errorf("parse frame: %w", err)
	}

	if header.MessageType != protocol.HelloRes {
		return nil, fmt.Errorf("unexpected response type: 0x%02x", header.MessageType)
	}

	return protocol.ParseHelloResponse(payload)
}
