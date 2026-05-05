#pragma once

#include "froth_types.h"
#include "frothy_value.h"

#define FROTH_SNAPSHOT_MAGIC "FRTHSNAP"
#define FROTH_SNAPSHOT_VERSION 0x0005
#define FROTH_SNAPSHOT_HEADER_SIZE 50
#define FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES                                       \
  (FROTH_SNAPSHOT_BLOCK_SIZE - FROTH_SNAPSHOT_HEADER_SIZE)

#define FROTH_SNAPSHOT_MIN_CELLSPACE_PAYLOAD_BYTES                             \
  (2u + 4u + 4u + 4u + (FROTH_DATA_SPACE_SIZE * (1u + sizeof(froth_cell_t))))
_Static_assert(
    FROTH_SNAPSHOT_BLOCK_SIZE > FROTH_SNAPSHOT_HEADER_SIZE,
    "Snapshot block size cannot be smaller than the snapshot header size.");
_Static_assert(FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES >=
                   FROTH_SNAPSHOT_MIN_CELLSPACE_PAYLOAD_BYTES,
               "Snapshot payload area is too small for the minimum full "
               "CellSpace prefix lower bound.");

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
  uint32_t payload_len;
  uint32_t generation;
  uint16_t flags;
} frothy_snapshot_header_info_t;

froth_error_t frothy_snapshot_save(void);
froth_error_t frothy_snapshot_restore(void);
froth_error_t frothy_snapshot_wipe(void);
froth_error_t frothy_snapshot_build_header(uint8_t *header,
                                           uint32_t payload_len,
                                           const uint8_t *payload,
                                           uint32_t generation);
froth_error_t
frothy_snapshot_parse_header(const uint8_t *header,
                             frothy_snapshot_header_info_t *parse_out);
froth_error_t frothy_snapshot_pick_active(uint8_t *slot_out,
                                          uint32_t *generation_out);
froth_error_t frothy_snapshot_pick_inactive(uint8_t *slot_out,
                                            uint32_t *next_generation_out);
froth_error_t frothy_builtin_save(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_restore(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_wipe(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out);
