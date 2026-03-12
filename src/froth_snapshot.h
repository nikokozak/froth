#pragma once
#include "froth_types.h"

#define FROTH_SNAPSHOT_MAGIC "FRTHSNAP"
#define FROTH_SNAPSHOT_VERSION 0x0004
#define FROTH_SNAPSHOT_MAX_BYTES 1024
#define FROTH_SNAPSHOT_MAX_OBJECTS 50
#define FROTH_SNAPSHOT_MAX_QUOTE_DEPTH 10
#define FROTH_SNAPSHOT_MAX_NAME_LEN 63
#define FROTH_SNAPSHOT_HEADER_SIZE 50 // bytes

// HEADER OFFSET CONSTANTS

#define FROTH_SNAPSHOT_MAGIC_OFFSET 0
#define FROTH_SNAPSHOT_VERSION_OFFSET 8
#define FROTH_SNAPSHOT_FLAGS_OFFSET 10
#define FROTH_SNAPSHOT_CELL_BITS_OFFSET 12
#define FROTH_SNAPSHOT_ENDIAN_OFFSET 13
#define FROTH_SNAPSHOT_ABI_HASH_OFFSET 14
#define FROTH_SNAPSHOT_GENERATION_OFFSET 18
#define FROTH_SNAPSHOT_PAYLOAD_LEN_OFFSET 22
#define FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET 26
#define FROTH_SNAPSHOT_HEADER_CRC32_OFFSET 30
#define FROTH_SNAPSHOT_RESERVED_OFFSET 34

typedef struct {
  uint8_t *data;
  froth_cell_u_t position;
} froth_snapshot_buffer_t;

typedef struct {
  uint32_t payload_len;
  uint32_t generation;
  uint16_t flags;
} froth_snapshot_header_info_t;

froth_error_t froth_snapshot_save(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot_buffer);
froth_error_t froth_snapshot_load(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot_buffer);

froth_error_t froth_snapshot_build_header(uint8_t *header, uint32_t payload_len,
                                          const uint8_t *payload,
                                          uint32_t generation);
froth_error_t
froth_snapshot_parse_header(const uint8_t *header,
                            froth_snapshot_header_info_t *parse_out);

uint32_t froth_snapshot_abi_hash(void);

#ifdef FROTH_HAS_SNAPSHOTS
#include "froth_ffi.h"
extern const froth_ffi_entry_t froth_snapshot_prims[];
#endif

/* A/B slot selection.
 * slot_out receives 0 or 1. generation_out receives the winning generation.
 * Returns FROTH_OK if a valid slot was found, FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT
 * if neither slot contains a valid snapshot (first boot). */
froth_error_t froth_snapshot_pick_active(uint8_t *slot_out,
                                         uint32_t *generation_out);

/* Returns the inactive slot (for save) and the next generation to use.
 * Always succeeds — if neither slot is valid, picks slot 0 with generation 1. */
froth_error_t froth_snapshot_pick_inactive(uint8_t *slot_out,
                                           uint32_t *next_generation_out);
