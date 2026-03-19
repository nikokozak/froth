package serial

import (
	"fmt"
	"log"
	"regexp"
	"time"

	"go.bug.st/serial"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
)

const (
	// ProbeDeadline is the total time allowed for the HELLO handshake,
	// including retries. Covers DTR-triggered reboot + boot sequence.
	ProbeDeadline = 5 * time.Second

	// DrainDuration exceeds the device's 750ms safe-boot window.
	DrainDuration = 1200 * time.Millisecond

	// Maximum HELLO_REQ sends before giving up.
	maxProbeRetries = 3
)

// candidatePattern matches likely USB-serial ports on macOS and Linux.
// Prefers /dev/cu.* on macOS (avoids /dev/tty.* which can cause
// additional DTR toggles during discovery).
var candidatePattern = regexp.MustCompile(
	`^/dev/cu\.(usbserial|usbmodem|SLAB_USBtoUART|USB|ACM)` +
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

		resp, err := ProbeHello(conn)
		if err == nil {
			return conn, resp, nil
		}
		conn.Close()
	}

	return nil, nil, fmt.Errorf("no Froth device found")
}

// ProbeHello sends HELLO_REQ and waits for a valid HELLO_RES, retrying
// on COBS decode errors and stale/garbage frames. This handles the
// ESP32 boot contamination that occurs after a DTR-triggered reset
// on macOS serial port open.
func ProbeHello(port *Port) (*protocol.HelloResponse, error) {
	deadline := time.Now().Add(ProbeDeadline)

	helloFrame, err := protocol.EncodeWireFrame(protocol.HelloReq, 0, nil)
	if err != nil {
		return nil, fmt.Errorf("build HELLO_REQ: %w", err)
	}

	// Flush any garbage from the OS serial buffer before starting.
	port.ResetInputBuffer()

	for attempt := 0; attempt < maxProbeRetries; attempt++ {
		if time.Now().After(deadline) {
			break
		}

		if err := port.Write(helloFrame); err != nil {
			return nil, fmt.Errorf("write: %w", err)
		}

		// Read frames until we get a valid HELLO_RES or run out of time.
		for {
			remaining := time.Until(deadline)
			if remaining <= 0 {
				break
			}
			// Cap per-frame read at 2 seconds so we can retry HELLO_REQ
			// if the device hasn't responded (e.g., still booting).
			readTimeout := remaining
			if readTimeout > 2*time.Second {
				readTimeout = 2 * time.Second
			}

			encoded, err := port.ReadFrame(readTimeout)
			if err != nil {
				// Timeout reading a frame — retry with a new HELLO_REQ.
				break
			}

			decoded, err := protocol.COBSDecode(encoded)
			if err != nil {
				log.Printf("probe: discard corrupt COBS (%v)", err)
				continue // Read next frame
			}

			header, payload, err := protocol.ParseFrame(decoded)
			if err != nil {
				log.Printf("probe: discard bad frame (%v)", err)
				continue
			}

			if header.MessageType != protocol.HelloRes {
				log.Printf("probe: discard unexpected type 0x%02x", header.MessageType)
				continue
			}

			return protocol.ParseHelloResponse(payload)
		}

		// Flush before retrying
		port.ResetInputBuffer()
	}

	return nil, fmt.Errorf("no HELLO_RES after %d attempts", maxProbeRetries)
}
