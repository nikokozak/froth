package protocol

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
)

// Payload formats use little-endian integers and length-prefixed strings
// (u16 length + raw UTF-8 bytes). These match the Frothy control payloads
// produced by src/frothy_control.c.

// --- HELLO ---

// HelloResponse holds parsed HELLO event payload fields.
type HelloResponse struct {
	CellBits     uint8
	MaxPayload   uint16
	HeapSize     uint32
	HeapUsed     uint32
	SlotCount    uint16
	Flags        uint8
	Version      string
	Board        string
	Capabilities []uint8
}

// ParseHelloResponse decodes a HELLO event binary payload.
func ParseHelloResponse(p []byte) (*HelloResponse, error) {
	// Payload layout from frothy_control_send_hello:
	//   u8   cell_bits
	//   u16  max_payload
	//   u32  heap_size
	//   u32  heap_used
	//   u16  slot_count
	//   u8   flags
	//   str  version        (u16 len + bytes)
	//   str  board          (u16 len + bytes)
	//   u8   capability_count
	//   u8   capabilities[] (reserved capability IDs)

	r := &payloadReader{data: p}

	h := &HelloResponse{}
	h.CellBits = r.u8()
	h.MaxPayload = r.u16()
	h.HeapSize = r.u32()
	h.HeapUsed = r.u32()
	h.SlotCount = r.u16()
	h.Flags = r.u8()
	h.Version = r.str()
	h.Board = r.str()

	capCount := r.u8()
	for i := uint8(0); i < capCount; i++ {
		h.Capabilities = append(h.Capabilities, r.u8())
	}

	if r.err != nil {
		return nil, fmt.Errorf("parse HELLO event: %w", r.err)
	}
	return h, nil
}

// GenerateSessionID returns a cryptographically random non-zero uint64.
func GenerateSessionID() (uint64, error) {
	for {
		var buf [8]byte
		if _, err := rand.Read(buf[:]); err != nil {
			return 0, fmt.Errorf("generate session ID: %w", err)
		}
		id := binary.LittleEndian.Uint64(buf[:])
		if id != 0 {
			return id, nil
		}
	}
}

// ParseOutputData reads OUTPUT_DATA payload: u16le byte_count + raw bytes.
func ParseOutputData(payload []byte) ([]byte, error) {
	if len(payload) < 2 {
		return nil, fmt.Errorf("output data too short: %d bytes", len(payload))
	}
	count := binary.LittleEndian.Uint16(payload[:2])
	if int(count) != len(payload)-2 {
		return nil, fmt.Errorf("output data truncated: want %d, have %d", count, len(payload)-2)
	}
	return payload[2 : 2+count], nil
}

// --- RESET ---

// ResetResponse holds parsed RESET value payload fields.
type ResetResponse struct {
	Status           uint32
	HeapSize         uint32
	HeapUsed         uint32
	HeapOverlayUsed  uint32
	SlotCount        uint16
	SlotOverlayCount uint16
	Flags            uint8
	Version          string
}

// ParseResetResponse decodes a RESET value binary payload.
func ParseResetResponse(p []byte) (*ResetResponse, error) {
	// Payload layout from frothy_control_send_reset_value:
	//   u32  status -- corresponds to the froth_error_t returned by froth_prim_dangerous_reset (0 if OK)
	//   u32  heap_size
	//   u32  heap_used
	//   u32  heap_overlay_used
	//   u16  slot_count
	//   u16  slot_overlay_count
	//   u8   flags
	//   str  version

	r := &payloadReader{data: p}

	reset := &ResetResponse{}
	reset.Status = r.u32()
	reset.HeapSize = r.u32()
	reset.HeapUsed = r.u32()
	reset.HeapOverlayUsed = r.u32()
	reset.SlotCount = r.u16()
	reset.SlotOverlayCount = r.u16()
	reset.Flags = r.u8()
	reset.Version = r.str()

	if r.err != nil {
		return nil, fmt.Errorf("parse RESET value: %w", r.err)
	}
	if reset.Status != 0 {
		return nil, fmt.Errorf("RESET value device reset error: %d", reset.Status)
	}
	return reset, nil
}

// --- Payload reader helper ---
// Cursor-based reader that tracks position and defers error checking.
// Same pattern as the payload_writer_t on the device side, but for reading.

type payloadReader struct {
	data []byte
	pos  int
	err  error
}

func (r *payloadReader) u8() uint8 {
	if r.err != nil || r.pos+1 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	v := r.data[r.pos]
	r.pos++
	return v
}

func (r *payloadReader) u16() uint16 {
	if r.err != nil || r.pos+2 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	v := binary.LittleEndian.Uint16(r.data[r.pos:])
	r.pos += 2
	return v
}

func (r *payloadReader) u32() uint32 {
	if r.err != nil || r.pos+4 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	v := binary.LittleEndian.Uint32(r.data[r.pos:])
	r.pos += 4
	return v
}

func (r *payloadReader) str() string {
	length := r.u16()
	if r.err != nil {
		return ""
	}
	if r.pos+int(length) > len(r.data) {
		r.err = fmt.Errorf("string overflows payload at offset %d (len=%d)", r.pos, length)
		return ""
	}
	s := string(r.data[r.pos : r.pos+int(length)])
	r.pos += int(length)
	return s
}
