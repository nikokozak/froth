#include "frothy_snapshot.h"

#include "froth_crc32.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_snapshot_codec.h"
#include "platform.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static frothy_runtime_t *frothy_runtime(void) {
  return &froth_vm.frothy_runtime;
}

static void frothy_snapshot_write_le16(uint8_t *buffer, uint16_t value) {
  buffer[0] = value & 0xFF;
  buffer[1] = (value >> 8) & 0xFF;
}

static void frothy_snapshot_write_le32(uint8_t *buffer, uint32_t value) {
  buffer[0] = value & 0xFF;
  buffer[1] = (value >> 8) & 0xFF;
  buffer[2] = (value >> 16) & 0xFF;
  buffer[3] = (value >> 24) & 0xFF;
}

static uint16_t frothy_snapshot_read_le16(const uint8_t *buffer) {
  return ((uint16_t)buffer[0]) | ((uint16_t)buffer[1] << 8);
}

static uint32_t frothy_snapshot_read_le32(const uint8_t *buffer) {
  return ((uint32_t)buffer[0]) | ((uint32_t)buffer[1] << 8) |
         ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 24);
}

static uint32_t frothy_snapshot_abi_hash(void) {
  uint8_t bytes[4];
  bytes[0] = FROTH_CELL_SIZE_BITS;
  bytes[1] = 0;
  bytes[2] = FROTH_SNAPSHOT_VERSION & 0xFF;
  bytes[3] = (FROTH_SNAPSHOT_VERSION >> 8) & 0xFF;
  return froth_crc32(bytes, sizeof(bytes));
}

static froth_error_t frothy_snapshot_reset_with_error(froth_error_t err) {
  froth_error_t reset_err = frothy_base_image_reset();
  return reset_err != FROTH_OK ? reset_err : err;
}

static uint32_t frothy_snapshot_stored_crc32(const uint8_t *header) {
  return frothy_snapshot_read_le32(
      &header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET]);
}

froth_error_t frothy_snapshot_build_header(uint8_t *header,
                                           uint32_t payload_len,
                                           const uint8_t *payload,
                                           uint32_t generation) {
  if (header == NULL || (payload_len > 0 && payload == NULL)) {
    return FROTH_ERROR_BOUNDS;
  }

  memset(header, 0, FROTH_SNAPSHOT_HEADER_SIZE);
  memcpy(&header[FROTH_SNAPSHOT_MAGIC_OFFSET], FROTH_SNAPSHOT_MAGIC, 8);
  frothy_snapshot_write_le16(&header[FROTH_SNAPSHOT_VERSION_OFFSET],
                             FROTH_SNAPSHOT_VERSION);
  frothy_snapshot_write_le16(&header[FROTH_SNAPSHOT_FLAGS_OFFSET], 0);
  header[FROTH_SNAPSHOT_CELL_BITS_OFFSET] = FROTH_CELL_SIZE_BITS;
  header[FROTH_SNAPSHOT_ENDIAN_OFFSET] = 0;
  frothy_snapshot_write_le32(&header[FROTH_SNAPSHOT_ABI_HASH_OFFSET],
                             frothy_snapshot_abi_hash());
  frothy_snapshot_write_le32(&header[FROTH_SNAPSHOT_GENERATION_OFFSET],
                             generation);
  frothy_snapshot_write_le32(&header[FROTH_SNAPSHOT_PAYLOAD_LEN_OFFSET],
                             payload_len);
  frothy_snapshot_write_le32(&header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET],
                             froth_crc32(payload, payload_len));
  frothy_snapshot_write_le32(&header[FROTH_SNAPSHOT_HEADER_CRC32_OFFSET],
                             froth_crc32(header, FROTH_SNAPSHOT_HEADER_SIZE));
  return FROTH_OK;
}

froth_error_t
frothy_snapshot_parse_header(const uint8_t *header,
                             frothy_snapshot_header_info_t *parse_out) {
  uint8_t copy[FROTH_SNAPSHOT_HEADER_SIZE];
  uint32_t stored_header_crc = 0;

  if (header == NULL || parse_out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  if (memcmp(&header[FROTH_SNAPSHOT_MAGIC_OFFSET], FROTH_SNAPSHOT_MAGIC, 8) !=
      0) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  memcpy(copy, header, FROTH_SNAPSHOT_HEADER_SIZE);
  stored_header_crc =
      frothy_snapshot_read_le32(&copy[FROTH_SNAPSHOT_HEADER_CRC32_OFFSET]);
  memset(&copy[FROTH_SNAPSHOT_HEADER_CRC32_OFFSET], 0, 4);
  if (froth_crc32(copy, FROTH_SNAPSHOT_HEADER_SIZE) != stored_header_crc) {
    return FROTH_ERROR_SNAPSHOT_BAD_CRC;
  }

  if (frothy_snapshot_read_le16(&header[FROTH_SNAPSHOT_VERSION_OFFSET]) !=
      FROTH_SNAPSHOT_VERSION) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  if (frothy_snapshot_read_le32(&header[FROTH_SNAPSHOT_ABI_HASH_OFFSET]) !=
      frothy_snapshot_abi_hash()) {
    return FROTH_ERROR_SNAPSHOT_INCOMPAT;
  }

  parse_out->payload_len =
      frothy_snapshot_read_le32(&header[FROTH_SNAPSHOT_PAYLOAD_LEN_OFFSET]);
  parse_out->generation =
      frothy_snapshot_read_le32(&header[FROTH_SNAPSHOT_GENERATION_OFFSET]);
  parse_out->flags =
      frothy_snapshot_read_le16(&header[FROTH_SNAPSHOT_FLAGS_OFFSET]);
  return FROTH_OK;
}

#ifdef FROTH_HAS_SNAPSHOTS
static froth_error_t
frothy_snapshot_read_slot_header(uint8_t slot,
                                 frothy_snapshot_header_info_t *info) {
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  froth_error_t err =
      platform_snapshot_read(slot, 0, header, FROTH_SNAPSHOT_HEADER_SIZE);
  if (err != FROTH_OK) {
    return err;
  }
  return frothy_snapshot_parse_header(header, info);
}

froth_error_t frothy_snapshot_pick_active(uint8_t *slot_out,
                                          uint32_t *generation_out) {
  frothy_snapshot_header_info_t info_a;
  frothy_snapshot_header_info_t info_b;
  froth_error_t err_a;
  froth_error_t err_b;
  bool a_valid;
  bool b_valid;

  if (slot_out == NULL || generation_out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  err_a = frothy_snapshot_read_slot_header(0, &info_a);
  err_b = frothy_snapshot_read_slot_header(1, &info_b);
  a_valid = err_a == FROTH_OK;
  b_valid = err_b == FROTH_OK;

  if (!a_valid && !b_valid) {
    return FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT;
  }

  if (a_valid && (!b_valid || info_a.generation >= info_b.generation)) {
    *slot_out = 0;
    *generation_out = info_a.generation;
  } else {
    *slot_out = 1;
    *generation_out = info_b.generation;
  }

  return FROTH_OK;
}

froth_error_t frothy_snapshot_pick_inactive(uint8_t *slot_out,
                                            uint32_t *next_generation_out) {
  frothy_snapshot_header_info_t info_a;
  frothy_snapshot_header_info_t info_b;
  froth_error_t err_a;
  froth_error_t err_b;
  bool a_valid;
  bool b_valid;

  if (slot_out == NULL || next_generation_out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  err_a = frothy_snapshot_read_slot_header(0, &info_a);
  err_b = frothy_snapshot_read_slot_header(1, &info_b);
  a_valid = err_a == FROTH_OK;
  b_valid = err_b == FROTH_OK;

  if (!a_valid && !b_valid) {
    *slot_out = 0;
    *next_generation_out = 1;
  } else if (a_valid && !b_valid) {
    *slot_out = 1;
    *next_generation_out = info_a.generation + 1;
  } else if (!a_valid && b_valid) {
    *slot_out = 0;
    *next_generation_out = info_b.generation + 1;
  } else if (info_a.generation <= info_b.generation) {
    *slot_out = 0;
    *next_generation_out = info_b.generation + 1;
  } else {
    *slot_out = 1;
    *next_generation_out = info_a.generation + 1;
  }

  return FROTH_OK;
}
#endif

froth_error_t frothy_snapshot_save(void) {
#ifndef FROTH_HAS_SNAPSHOTS
  return FROTH_ERROR_IO;
#else
  const uint8_t *payload = NULL;
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  uint8_t slot = 0;
  uint32_t generation = 0;
  uint32_t payload_length = 0;
  froth_error_t err;

  err = frothy_snapshot_codec_write_payload(frothy_runtime(), &payload,
                                            &payload_length);
  if (err == FROTH_OK) {
    err = frothy_snapshot_pick_inactive(&slot, &generation);
  }
  if (err == FROTH_OK) {
    err = platform_snapshot_write(slot, FROTH_SNAPSHOT_HEADER_SIZE, payload,
                                  payload_length);
  }
  if (err == FROTH_OK) {
    err = frothy_snapshot_build_header(header, payload_length, payload,
                                       generation);
  }
  if (err == FROTH_OK) {
    err = platform_snapshot_write(slot, 0, header, sizeof(header));
  }

  return err;
#endif
}

froth_error_t frothy_snapshot_restore(void) {
#ifndef FROTH_HAS_SNAPSHOTS
  return FROTH_ERROR_IO;
#else
  uint8_t slot = 0;
  uint32_t generation = 0;
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  frothy_snapshot_header_info_t info;
  uint8_t *payload = NULL;
  size_t payload_capacity = 0;
  froth_error_t err;

  err = frothy_snapshot_pick_active(&slot, &generation);
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  err = platform_snapshot_read(slot, 0, header, sizeof(header));
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  err = frothy_snapshot_parse_header(header, &info);
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }
  if (info.payload_len > FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES) {
    return frothy_snapshot_reset_with_error(FROTH_ERROR_SNAPSHOT_OVERFLOW);
  }

  payload = frothy_snapshot_codec_payload_buffer(&payload_capacity);
  if ((size_t)info.payload_len > payload_capacity) {
    return frothy_snapshot_reset_with_error(FROTH_ERROR_SNAPSHOT_OVERFLOW);
  }

  err = platform_snapshot_read(slot, FROTH_SNAPSHOT_HEADER_SIZE, payload,
                               info.payload_len);
  if (err == FROTH_OK &&
      froth_crc32(payload, info.payload_len) !=
          frothy_snapshot_stored_crc32(header)) {
    err = FROTH_ERROR_SNAPSHOT_BAD_CRC;
  }
  if (err == FROTH_OK) {
    err = frothy_snapshot_codec_validate_payload(payload, info.payload_len);
  }
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  err = frothy_base_image_reset();
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_snapshot_codec_restore_payload(payload, info.payload_len);
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  return FROTH_OK;
#endif
}

froth_error_t frothy_snapshot_wipe(void) {
#ifndef FROTH_HAS_SNAPSHOTS
  return frothy_base_image_reset();
#else
  FROTH_TRY(platform_snapshot_erase(0));
  FROTH_TRY(platform_snapshot_erase(1));
  return frothy_base_image_reset();
#endif
}

froth_error_t frothy_builtin_save(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_snapshot_save());
  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_restore(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_snapshot_restore());
  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_wipe(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_snapshot_wipe());
  *out = frothy_value_make_nil();
  return FROTH_OK;
}
