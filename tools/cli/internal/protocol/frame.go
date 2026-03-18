package protocol

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

// Frame layout constants matching ADR-033 and froth_transport.h.
const (
	HeaderSize      = 12
	MaxPayload      = 256
	Magic0          = 'F'
	Magic1          = 'L'
	ProtocolVersion = 1
)

// Message types matching froth_transport.h.
const (
	HelloReq   = 0x01
	HelloRes   = 0x02
	EvalReq    = 0x03
	EvalRes    = 0x04
	InspectReq = 0x05
	InspectRes = 0x06
	InfoReq    = 0x07
	InfoRes    = 0x08
	ResetReq   = 0x09
	ResetRes   = 0x0A
	Event      = 0xFE
	Error      = 0xFF
)

// Sentinel request ID for unparseable requests.
const ReqIDNone = 0xFFFF

// Header represents a parsed FROTH-LINK/1 frame header.
type Header struct {
	MessageType   byte
	RequestID     uint16
	PayloadLength uint16
	CRC32         uint32
}

// BuildFrame constructs a complete raw frame (header + payload) with
// computed CRC32. Returns the frame bytes ready for COBS encoding.
// This mirrors froth_link_header_build in froth_transport.c.
func BuildFrame(msgType byte, requestID uint16, payload []byte) ([]byte, error) {
	// Frame layout (12-byte header + N payload bytes):
	//   [0..1]   magic "FL"
	//   [2]      version = 1
	//   [3]      message_type
	//   [4..5]   request_id (LE)
	//   [6..7]   payload_length (LE)
	//   [8..11]  crc32 (LE)
	//   [12..N]  payload
	//
	// CRC32 covers header[0..7] concatenated with payload.
	// This is IEEE CRC32 (same polynomial as Go's crc32.IEEE).

	plen := len(payload)
	if plen > MaxPayload {
		return nil, fmt.Errorf("payload too large: %d > %d", plen, MaxPayload)
	}

	frame := make([]byte, HeaderSize+plen)

	// Header fields
	frame[0] = Magic0
	frame[1] = Magic1
	frame[2] = ProtocolVersion
	frame[3] = msgType
	binary.LittleEndian.PutUint16(frame[4:6], requestID)
	binary.LittleEndian.PutUint16(frame[6:8], uint16(plen))

	// Copy payload
	copy(frame[HeaderSize:], payload)

	// CRC32 over header[0..7] + payload
	crcData := make([]byte, 8+plen)
	copy(crcData[:8], frame[:8])
	copy(crcData[8:], payload)
	checksum := crc32.ChecksumIEEE(crcData)
	binary.LittleEndian.PutUint32(frame[8:12], checksum)

	return frame, nil
}

// ParseFrame validates and parses a decoded (post-COBS) frame.
// Returns the header and payload slice, or an error.
// This mirrors froth_link_header_parse in froth_transport.c.
func ParseFrame(frame []byte) (*Header, []byte, error) {
	// Validation order (matches device side):
	// 1. Frame must be at least 12 bytes (header size).
	// 2. Magic must be "FL".
	// 3. Version must be 1.
	// 4. Read message_type, request_id, payload_length, crc32 (all LE).
	// 5. payload_length must not exceed MaxPayload.
	// 6. Frame must be at least HeaderSize + payload_length bytes.
	// 7. Compute CRC32 over header[0..7] + payload. Must match.

	if len(frame) < HeaderSize {
		return nil, nil, fmt.Errorf("frame too short: %d bytes", len(frame))
	}

	if frame[0] != Magic0 || frame[1] != Magic1 {
		return nil, nil, fmt.Errorf("bad magic: %c%c", frame[0], frame[1])
	}

	if frame[2] != ProtocolVersion {
		return nil, nil, fmt.Errorf("unsupported version: %d", frame[2])
	}

	h := &Header{
		MessageType:   frame[3],
		RequestID:     binary.LittleEndian.Uint16(frame[4:6]),
		PayloadLength: binary.LittleEndian.Uint16(frame[6:8]),
		CRC32:         binary.LittleEndian.Uint32(frame[8:12]),
	}

	if h.PayloadLength > MaxPayload {
		return nil, nil, fmt.Errorf("payload too large: %d", h.PayloadLength)
	}

	total := HeaderSize + int(h.PayloadLength)
	if len(frame) < total {
		return nil, nil, fmt.Errorf("frame truncated: need %d, have %d", total, len(frame))
	}

	payload := frame[HeaderSize:total]

	// CRC check
	crcData := make([]byte, 8+len(payload))
	copy(crcData[:8], frame[:8])
	copy(crcData[8:], payload)
	expected := crc32.ChecksumIEEE(crcData)

	if expected != h.CRC32 {
		return nil, nil, fmt.Errorf("CRC mismatch: expected %08x, got %08x", expected, h.CRC32)
	}

	return h, payload, nil
}

// EncodeWireFrame builds a complete wire frame: 0x00 + COBS(raw) + 0x00.
// This is what gets written to the serial port.
func EncodeWireFrame(msgType byte, requestID uint16, payload []byte) ([]byte, error) {
	raw, err := BuildFrame(msgType, requestID, payload)
	if err != nil {
		return nil, err
	}

	encoded := COBSEncode(raw)

	// Wire format: 0x00 delimiter + encoded + 0x00 delimiter
	wire := make([]byte, 1+len(encoded)+1)
	wire[0] = 0x00
	copy(wire[1:], encoded)
	wire[len(wire)-1] = 0x00

	return wire, nil
}
