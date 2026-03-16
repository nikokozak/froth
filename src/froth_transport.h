#pragma once
#include "froth_types.h"
#include <stdint.h>

#ifndef FROTH_LINK_MAX_PAYLOAD
#define FROTH_LINK_MAX_PAYLOAD 256
#endif

/* Raw frame = 12-byte header + payload. COBS adds at most 1 byte per 254. */
#define FROTH_LINK_MAX_FRAME (12 + FROTH_LINK_MAX_PAYLOAD)
#define FROTH_LINK_COBS_MAX  (FROTH_LINK_MAX_FRAME + (FROTH_LINK_MAX_FRAME / 254) + 1)

#define FROTH_LINK_HEADER_SIZE 12
#define FROTH_LINK_MAGIC_0 'F'
#define FROTH_LINK_MAGIC_1 'L'
#define FROTH_LINK_VERSION 1

/* Message types (Phase 1) */
#define FROTH_LINK_HELLO_REQ   0x01
#define FROTH_LINK_HELLO_RES   0x02
#define FROTH_LINK_EVAL_REQ    0x03
#define FROTH_LINK_EVAL_RES    0x04
#define FROTH_LINK_INSPECT_REQ 0x05
#define FROTH_LINK_INSPECT_RES 0x06
#define FROTH_LINK_INFO_REQ    0x07
#define FROTH_LINK_INFO_RES    0x08
#define FROTH_LINK_EVENT       0xFE
#define FROTH_LINK_ERROR       0xFF

/* Sentinel request ID for unparseable requests */
#define FROTH_LINK_REQ_ID_NONE 0xFFFF

typedef struct {
  uint8_t message_type;
  uint16_t request_id;
  uint16_t payload_length;
  uint32_t crc32;
} froth_link_header_t;

/* ── COBS codec ──────────────────────────────────────────────────────
 * Encode/decode support in-place operation (out == in) for decode only.
 * Encode output is always larger than input; decode output is always
 * smaller or equal.                                                   */

froth_error_t froth_cobs_encode(const uint8_t *in, uint16_t in_len,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len);

froth_error_t froth_cobs_decode(const uint8_t *in, uint16_t in_len,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len);

/* ── Frame header ────────────────────────────────────────────────────
 * parse validates magic, version, payload cap, and CRC.
 * On success, *payload points into the frame buffer at offset 12.
 * build serializes header + payload and computes CRC.                 */

froth_error_t froth_link_header_parse(const uint8_t *frame, uint16_t frame_len,
                                      froth_link_header_t *header,
                                      const uint8_t **payload);

froth_error_t froth_link_header_build(uint8_t message_type, uint16_t request_id,
                                      const uint8_t *payload,
                                      uint16_t payload_len, uint8_t *out,
                                      uint16_t out_cap, uint16_t *out_len);

/* ── Full frame send ─────────────────────────────────────────────────
 * Builds header, COBS-encodes, emits 0x00 + encoded + 0x00 via
 * platform_emit. Uses a static internal buffer (one frame at a time). */

froth_error_t froth_link_send_frame(uint8_t message_type, uint16_t request_id,
                                    const uint8_t *payload,
                                    uint16_t payload_len);

/* ── Inbound frame accumulation ─────────────────────────────────────
 * The mux calls frame_reset when a 0x00 starts a frame, frame_byte
 * for each byte between delimiters, and frame_complete when the
 * terminating 0x00 arrives. frame_complete COBS-decodes in-place,
 * parses the header, and dispatches to the request handler.
 * Invalid frames are silently dropped (per ADR-033 resync rules).    */

void froth_link_frame_reset(void);
froth_error_t froth_link_frame_byte(uint8_t byte);
froth_error_t froth_link_frame_complete(froth_vm_t *vm);
