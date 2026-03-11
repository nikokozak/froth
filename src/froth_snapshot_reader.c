#include "froth_heap.h"
#include "froth_slot_table.h"
#include "froth_snapshot.h"
#include "froth_types.h"
#include "froth_vm.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint8_t *data;
  froth_cell_u_t length;
  froth_cell_u_t cursor;
} snapshot_reader_t;

froth_error_t read_u8(snapshot_reader_t *reader, uint8_t *output) {
  if (reader->cursor + 1 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *output = reader->data[reader->cursor];
  reader->cursor++;
  return FROTH_OK;
}

froth_error_t read_u16(snapshot_reader_t *reader, uint16_t *output) {
  if (reader->cursor + 2 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *output = (uint16_t)reader->data[reader->cursor] |
            ((uint16_t)reader->data[reader->cursor + 1] << 8);
  reader->cursor += 2;
  return FROTH_OK;
}

froth_error_t read_u32(snapshot_reader_t *reader, uint32_t *output) {
  if (reader->cursor + 4 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *output = (uint32_t)reader->data[reader->cursor] |
            ((uint32_t)reader->data[reader->cursor + 1] << 8) |
            ((uint32_t)reader->data[reader->cursor + 2] << 16) |
            ((uint32_t)reader->data[reader->cursor + 3] << 24);
  reader->cursor += 4;
  return FROTH_OK;
}

froth_error_t read_u64(snapshot_reader_t *reader, uint64_t *output) {
  if (reader->cursor + 8 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *output = (uint64_t)reader->data[reader->cursor] |
            ((uint64_t)reader->data[reader->cursor + 1] << 8) |
            ((uint64_t)reader->data[reader->cursor + 2] << 16) |
            ((uint64_t)reader->data[reader->cursor + 3] << 24) |
            ((uint64_t)reader->data[reader->cursor + 4] << 32) |
            ((uint64_t)reader->data[reader->cursor + 5] << 40) |
            ((uint64_t)reader->data[reader->cursor + 6] << 48) |
            ((uint64_t)reader->data[reader->cursor + 7] << 56);
  reader->cursor += 8;
  return FROTH_OK;
}

froth_error_t read_cell(snapshot_reader_t *reader, froth_cell_t *output) {
#if FROTH_CELL_SIZE_BITS == 8
  return read_u8(reader, (uint8_t *)output);
#elif FROTH_CELL_SIZE_BITS == 16
  return read_u16(reader, (uint16_t *)output);
#elif FROTH_CELL_SIZE_BITS == 32
  return read_u32(reader, (uint32_t *)output);
#elif FROTH_CELL_SIZE_BITS == 64
  return read_u64(reader, (uint64_t *)output);
#endif
}

froth_error_t read_bytes(snapshot_reader_t *reader, froth_cell_u_t num_bytes,
                         uint8_t *output_bytes) {
  if (reader->cursor + num_bytes > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  for (int i = 0; i < num_bytes; i++) {
    output_bytes[i] = reader->data[reader->cursor];
    reader->cursor++;
  }

  return FROTH_OK;
}

froth_error_t reset_overlay_to_base(froth_vm_t *froth_vm) {
  froth_vm->heap.pointer = froth_vm->watermark_heap_offset;
  FROTH_TRY(froth_slot_reset_pointer_to_overlay_watermark());
  return FROTH_OK;
}

froth_error_t read_names(froth_vm_t *froth_vm, snapshot_reader_t *reader,
                         froth_cell_u_t *output_names) {
  uint16_t name_count;
  uint8_t name[FROTH_SNAPSHOT_MAX_NAME_LEN + 1]; // With terminator
  uint16_t name_len;
  froth_cell_u_t name_slot_idx;

  // Read the name, create the slot, save the slot number so that
  // name_id<->slot_idx in names array
  FROTH_TRY(read_u16(reader, &name_count));
  for (int i = 0; i < name_count; i++) {
    FROTH_TRY(read_u16(reader, &name_len));
    if (name_len > FROTH_SNAPSHOT_MAX_NAME_LEN) {
      return FROTH_ERROR_SNAPSHOT_OVERFLOW; // TODO: better error
    }

    FROTH_TRY(read_bytes(reader, (froth_cell_u_t)name_len, name));
    name[name_len] = '\0';

    FROTH_TRY(froth_slot_find_name_or_create(
        &froth_vm->heap, (const char *)name, &name_slot_idx));
    output_names[i] = name_slot_idx;
  }

  return FROTH_OK;
}

// --- Object loading: read directly from snapshot stream into heap ---

static froth_error_t load_quote_token(snapshot_reader_t *reader,
                                      froth_cell_u_t *names,
                                      froth_cell_t *objects,
                                      froth_cell_t *out_cell) {
  uint8_t tag;
  FROTH_TRY(read_u8(reader, &tag));

  switch (tag) {
  case FROTH_NUMBER: {
    froth_cell_t value;
    FROTH_TRY(read_cell(reader, &value));
    return froth_make_cell(value, FROTH_NUMBER, out_cell);
  }
  case FROTH_QUOTE:
  case FROTH_PATTERN:
  case FROTH_BSTRING:
  case FROTH_CONTRACT: {
    uint32_t obj_id;
    FROTH_TRY(read_u32(reader, &obj_id));
    *out_cell = objects[obj_id]; // already tagged from earlier load
    return FROTH_OK;
  }
  case FROTH_CALL:
  case FROTH_SLOT: {
    uint16_t name_id;
    FROTH_TRY(read_u16(reader, &name_id));
    return froth_make_cell(names[name_id], tag, out_cell);
  }
  default:
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
}

static froth_error_t load_quote_object(froth_vm_t *froth_vm,
                                       snapshot_reader_t *reader,
                                       froth_cell_u_t *names,
                                       froth_cell_t *objects,
                                       froth_cell_t *out_cell) {
  uint16_t tok_count;
  froth_cell_t *cells;
  froth_cell_u_t heap_location;

  FROTH_TRY(read_u16(reader, &tok_count));
  FROTH_TRY(froth_heap_allocate_cells(tok_count + 1, &froth_vm->heap,
                                      &cells, &heap_location));

  cells[0] = tok_count;
  for (uint16_t i = 0; i < tok_count; i++) {
    FROTH_TRY(load_quote_token(reader, names, objects, &cells[i + 1]));
  }

  return froth_make_cell(heap_location, FROTH_QUOTE, out_cell);
}

static froth_error_t load_pattern_object(froth_vm_t *froth_vm,
                                         snapshot_reader_t *reader,
                                         froth_cell_t *out_cell) {
  uint8_t pattern_length;
  froth_cell_u_t heap_location;

  FROTH_TRY(read_u8(reader, &pattern_length));
  FROTH_TRY(froth_heap_allocate_bytes(pattern_length + 1, &froth_vm->heap,
                                      &heap_location));

  uint8_t *heap = &froth_vm->heap.data[heap_location];
  heap[0] = pattern_length;
  FROTH_TRY(read_bytes(reader, pattern_length, &heap[1]));

  return froth_make_cell(heap_location, FROTH_PATTERN, out_cell);
}

static froth_error_t load_bstring_object(froth_vm_t *froth_vm,
                                         snapshot_reader_t *reader,
                                         froth_cell_t *out_cell) {
  uint16_t string_length;
  froth_cell_u_t heap_location;

  FROTH_TRY(read_u16(reader, &string_length));
  FROTH_TRY(froth_heap_allocate_bytes(sizeof(froth_cell_t) + string_length + 1,
                                      &froth_vm->heap, &heap_location));

  uint8_t *heap = &froth_vm->heap.data[heap_location];
  ((froth_cell_t *)heap)[0] = (froth_cell_t)string_length;
  FROTH_TRY(read_bytes(reader, string_length, &heap[sizeof(froth_cell_t)]));
  heap[sizeof(froth_cell_t) + string_length] = '\0';

  return froth_make_cell(heap_location, FROTH_BSTRING, out_cell);
}

static froth_error_t load_object(froth_vm_t *froth_vm,
                                 snapshot_reader_t *reader,
                                 froth_cell_u_t *names,
                                 froth_cell_t *objects,
                                 froth_cell_t *out_cell) {
  uint8_t obj_kind;
  uint32_t obj_id;
  uint32_t obj_len;

  FROTH_TRY(read_u8(reader, &obj_kind));
  FROTH_TRY(read_u32(reader, &obj_id));
  FROTH_TRY(read_u32(reader, &obj_len));
  (void)obj_id;  // validated implicitly by positional indexing
  (void)obj_len; // useful later for skipping unknown object kinds

  switch (obj_kind) {
  case FROTH_QUOTE:
    return load_quote_object(froth_vm, reader, names, objects, out_cell);
  case FROTH_PATTERN:
    return load_pattern_object(froth_vm, reader, out_cell);
  case FROTH_BSTRING:
    return load_bstring_object(froth_vm, reader, out_cell);
  default:
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
}

static froth_error_t load_objects(froth_vm_t *froth_vm,
                                  snapshot_reader_t *reader,
                                  froth_cell_u_t *names,
                                  froth_cell_t *objects) {
  uint32_t obj_count;
  FROTH_TRY(read_u32(reader, &obj_count));

  for (uint32_t i = 0; i < obj_count; i++) {
    FROTH_TRY(load_object(froth_vm, reader, names, objects, &objects[i]));
  }

  return FROTH_OK;
}

// --- Slot bindings: resolve impl values and apply to slot table ---

static froth_error_t decode_binding_impl(snapshot_reader_t *reader,
                                         froth_cell_u_t *names,
                                         froth_cell_t *objects,
                                         froth_cell_t *out_cell) {
  uint8_t impl_kind;
  FROTH_TRY(read_u8(reader, &impl_kind));

  switch (impl_kind) {
  case FROTH_NUMBER: {
    froth_cell_t value;
    FROTH_TRY(read_cell(reader, &value));
    return froth_make_cell(value, FROTH_NUMBER, out_cell);
  }
  case FROTH_QUOTE:
  case FROTH_PATTERN:
  case FROTH_BSTRING:
  case FROTH_CONTRACT: {
    uint32_t obj_id;
    FROTH_TRY(read_u32(reader, &obj_id));
    *out_cell = objects[obj_id];
    return FROTH_OK;
  }
  case FROTH_SLOT: {
    uint16_t name_id;
    FROTH_TRY(read_u16(reader, &name_id));
    return froth_make_cell(names[name_id], FROTH_SLOT, out_cell);
  }
  default:
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
}

static froth_error_t load_bindings(snapshot_reader_t *reader,
                                   froth_cell_u_t *names,
                                   froth_cell_t *objects) {
  uint32_t slot_count;
  FROTH_TRY(read_u32(reader, &slot_count));

  for (uint32_t i = 0; i < slot_count; i++) {
    uint16_t name_id;
    froth_cell_t impl_cell;
    uint32_t contract_obj_id;
    uint16_t meta_flags;
    uint16_t meta_len;

    FROTH_TRY(read_u16(reader, &name_id));
    FROTH_TRY(decode_binding_impl(reader, names, objects, &impl_cell));

    // Reserved fields — read and discard
    FROTH_TRY(read_u32(reader, &contract_obj_id));
    FROTH_TRY(read_u16(reader, &meta_flags));
    FROTH_TRY(read_u16(reader, &meta_len));
    (void)contract_obj_id;
    (void)meta_flags;
    (void)meta_len;

    froth_cell_u_t slot_index = names[name_id];
    FROTH_TRY(froth_slot_set_impl(slot_index, impl_cell));
    FROTH_TRY(froth_slot_set_overlay(slot_index, 1));
  }

  return FROTH_OK;
}

// --- Top-level load ---

froth_error_t froth_snapshot_load(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot_buffer) {
  snapshot_reader_t reader = {.data = snapshot_buffer->data,
                              .length = snapshot_buffer->position,
                              .cursor = 0};

  froth_cell_u_t names[FROTH_SLOT_TABLE_SIZE];
  froth_cell_t objects[FROTH_SNAPSHOT_MAX_OBJECTS];

  FROTH_TRY(reset_overlay_to_base(froth_vm));

  FROTH_TRY(read_names(froth_vm, &reader, names));
  FROTH_TRY(load_objects(froth_vm, &reader, names, objects));
  FROTH_TRY(load_bindings(&reader, names, objects));

  return FROTH_OK;
}
