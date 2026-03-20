#include "froth_heap.h"
#include "froth_slot_table.h"
#include "froth_snapshot.h"
#include "froth_tbuf.h"
#include "froth_types.h"
#include "froth_vm.h"
#include <stdint.h>
#include <string.h>

/* Type aliases for brevity — actual types live in froth_snapshot.h */
typedef froth_snapshot_name_item_t name_table_item_t;
typedef froth_snapshot_name_table_t name_table_t;
typedef froth_snapshot_object_item_t object_table_item_t;
typedef froth_snapshot_object_table_t object_table_t;
typedef froth_snapshot_walk_frame_t quote_walk_frame_t;
typedef froth_snapshot_walk_stack_t quote_walk_stack_t;

static bool name_table_has_slot(const name_table_t *name_table,
                                froth_cell_u_t slot_index) {
  for (froth_cell_u_t i = 0; i < name_table->count; i++) {
    if (name_table->items[i].slot_index == slot_index) {
      return true;
    }
  }

  return false;
}

static froth_error_t name_table_find_id(const name_table_t *name_table,
                                        froth_cell_u_t slot_index,
                                        froth_cell_u_t *name_id) {
  for (froth_cell_u_t i = 0; i < name_table->count; i++) {
    if (name_table->items[i].slot_index == slot_index) {
      *name_id = i;
      return FROTH_OK;
    }
  }

  return FROTH_ERROR_SNAPSHOT_UNRESOLVED;
}

static froth_error_t name_table_add_slot(name_table_t *name_table,
                                         froth_cell_u_t slot_index) {
  const char *slot_name;

  if (name_table_has_slot(name_table, slot_index)) {
    return FROTH_OK;
  }

  if (name_table->count >= FROTH_SLOT_TABLE_SIZE) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  FROTH_TRY(froth_slot_get_name(slot_index, &slot_name));

  name_table->items[name_table->count].name = slot_name;
  name_table->items[name_table->count].slot_index = slot_index;
  name_table->count++;

  return FROTH_OK;
}

static bool object_table_has_offset(const object_table_t *object_table,
                                    froth_cell_u_t heap_offset) {
  for (froth_cell_u_t i = 0; i < object_table->count; i++) {
    if (object_table->items[i].heap_offset == heap_offset) {
      return true;
    }
  }

  return false;
}

static froth_error_t object_table_find_id(const object_table_t *object_table,
                                          froth_cell_u_t heap_offset,
                                          froth_cell_u_t *object_id) {
  for (froth_cell_u_t i = 0; i < object_table->count; i++) {
    if (object_table->items[i].heap_offset == heap_offset) {
      *object_id = i;
      return FROTH_OK;
    }
  }

  return FROTH_ERROR_SNAPSHOT_UNRESOLVED;
}

static froth_error_t object_table_add_if_missing(object_table_t *object_table,
                                                 froth_cell_u_t heap_offset,
                                                 froth_cell_tag_t type) {
  if (object_table_has_offset(object_table, heap_offset)) {
    return FROTH_OK;
  }

  if (object_table->count >= FROTH_SNAPSHOT_MAX_OBJECTS) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  object_table->items[object_table->count].object_id = object_table->count;
  object_table->items[object_table->count].heap_offset = heap_offset;
  object_table->items[object_table->count].type = type;
  object_table->count++;

  return FROTH_OK;
}

static void quote_walk_stack_reset(quote_walk_stack_t *stack) {
  stack->depth = 0;
}

static froth_error_t quote_walk_stack_push(quote_walk_stack_t *stack,
                                           froth_cell_u_t quote_heap_offset,
                                           froth_cell_u_t next_token_index) {
  if (stack->depth >= FROTH_SNAPSHOT_MAX_QUOTE_DEPTH) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  stack->frames[stack->depth].quote_heap_offset = quote_heap_offset;
  stack->frames[stack->depth].next_token_index = next_token_index;
  stack->depth++;

  return FROTH_OK;
}

static froth_error_t quote_walk_stack_pop(quote_walk_stack_t *stack,
                                          quote_walk_frame_t *frame) {
  if (stack->depth == 0) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  stack->depth--;
  *frame = stack->frames[stack->depth];

  return FROTH_OK;
}

static froth_error_t collect_cell_dependencies(froth_cell_t cell,
                                               name_table_t *name_table,
                                               object_table_t *object_table) {
  if (FROTH_CELL_IS_BSTRING(cell)) {
    if (FROTH_BSTRING_IS_TRANSIENT(FROTH_CELL_STRIP_TAG(cell)))
      return FROTH_ERROR_SNAPSHOT_TRANSIENT;
    return object_table_add_if_missing(object_table, FROTH_CELL_STRIP_TAG(cell),
                                       FROTH_CELL_GET_TAG(cell));
  }

  if (FROTH_CELL_IS_PATTERN(cell)) {
    return object_table_add_if_missing(object_table, FROTH_CELL_STRIP_TAG(cell),
                                       FROTH_CELL_GET_TAG(cell));
  }

  if (FROTH_CELL_IS_CALL(cell) || FROTH_CELL_IS_SLOT(cell)) {
    return name_table_add_slot(name_table, FROTH_CELL_STRIP_TAG(cell));
  }

  return FROTH_OK;
}

static froth_error_t collect_quote_dependencies(froth_vm_t *froth_vm,
                                                froth_cell_t quote_impl,
                                                name_table_t *name_table,
                                                object_table_t *object_table) {
  quote_walk_stack_t walk_stack = {0};
  froth_cell_u_t root_offset = FROTH_CELL_STRIP_TAG(quote_impl);

  quote_walk_stack_reset(&walk_stack);
  FROTH_TRY(quote_walk_stack_push(&walk_stack, root_offset, 1));

  while (walk_stack.depth > 0) {
    quote_walk_frame_t frame;
    froth_cell_t *quote_cells;
    froth_cell_u_t quote_length;
    bool descended_into_child = false;

    FROTH_TRY(quote_walk_stack_pop(&walk_stack, &frame));

    if (object_table_has_offset(object_table, frame.quote_heap_offset)) {
      continue;
    }

    quote_cells = froth_heap_cell_ptr(&froth_vm->heap, frame.quote_heap_offset);
    quote_length = (froth_cell_u_t)quote_cells[0];

    for (froth_cell_u_t token_index = frame.next_token_index;
         token_index <= quote_length; token_index++) {
      froth_cell_t token = quote_cells[token_index];

      if (FROTH_CELL_IS_QUOTE(token)) {
        FROTH_TRY(quote_walk_stack_push(&walk_stack, frame.quote_heap_offset,
                                        token_index + 1));
        FROTH_TRY(
            quote_walk_stack_push(&walk_stack, FROTH_CELL_STRIP_TAG(token), 1));
        descended_into_child = true;
        break;
      }

      FROTH_TRY(collect_cell_dependencies(token, name_table, object_table));
    }

    if (!descended_into_child) {
      FROTH_TRY(object_table_add_if_missing(
          object_table, frame.quote_heap_offset, FROTH_QUOTE));
    }
  }

  return FROTH_OK;
}

static froth_error_t
collect_snapshot_dependencies(froth_vm_t *froth_vm, name_table_t *name_table,
                              object_table_t *object_table) {
  froth_cell_u_t slot_count = froth_slot_count();

  for (froth_cell_u_t slot_index = 0; slot_index < slot_count; slot_index++) {
    froth_cell_t slot_impl;

    if (!froth_slot_is_overlay(slot_index)) {
      continue;
    }

    FROTH_TRY(froth_slot_get_impl(slot_index, &slot_impl));

    if (FROTH_CELL_IS_BSTRING(slot_impl)) {
      if (FROTH_BSTRING_IS_TRANSIENT(FROTH_CELL_STRIP_TAG(slot_impl))) {
        return FROTH_ERROR_SNAPSHOT_TRANSIENT;
      }
    }

    FROTH_TRY(name_table_add_slot(name_table, slot_index));

    if (FROTH_CELL_IS_QUOTE(slot_impl)) {
      FROTH_TRY(collect_quote_dependencies(froth_vm, slot_impl, name_table,
                                           object_table));
      continue;
    }

    if (FROTH_CELL_IS_PATTERN(slot_impl) || FROTH_CELL_IS_BSTRING(slot_impl)) {
      FROTH_TRY(object_table_add_if_missing(object_table,
                                            FROTH_CELL_STRIP_TAG(slot_impl),
                                            FROTH_CELL_GET_TAG(slot_impl)));
    }
  }

  return FROTH_OK;
}

static froth_error_t emit_u8(froth_snapshot_buffer_t *snapshot, uint8_t value) {
  if (snapshot->position + 1 > FROTH_SNAPSHOT_MAX_BYTES) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  snapshot->data[snapshot->position] = value;
  snapshot->position++;

  return FROTH_OK;
}

static froth_error_t emit_u16(froth_snapshot_buffer_t *snapshot,
                              uint16_t value) {
  if (snapshot->position + 2 > FROTH_SNAPSHOT_MAX_BYTES) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  snapshot->data[snapshot->position] = value & 0xFF;
  snapshot->data[snapshot->position + 1] = (value >> 8) & 0xFF;
  snapshot->position += 2;

  return FROTH_OK;
}

static froth_error_t emit_u32(froth_snapshot_buffer_t *snapshot,
                              uint32_t value) {
  if (snapshot->position + 4 > FROTH_SNAPSHOT_MAX_BYTES) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  snapshot->data[snapshot->position] = value & 0xFF;
  snapshot->data[snapshot->position + 1] = (value >> 8) & 0xFF;
  snapshot->data[snapshot->position + 2] = (value >> 16) & 0xFF;
  snapshot->data[snapshot->position + 3] = (value >> 24) & 0xFF;
  snapshot->position += 4;

  return FROTH_OK;
}

static froth_error_t emit_u64(froth_snapshot_buffer_t *snapshot,
                              uint64_t value) {
  if (snapshot->position + 8 > FROTH_SNAPSHOT_MAX_BYTES) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  snapshot->data[snapshot->position] = value & 0xFF;
  snapshot->data[snapshot->position + 1] = (value >> 8) & 0xFF;
  snapshot->data[snapshot->position + 2] = (value >> 16) & 0xFF;
  snapshot->data[snapshot->position + 3] = (value >> 24) & 0xFF;
  snapshot->data[snapshot->position + 4] = (value >> 32) & 0xFF;
  snapshot->data[snapshot->position + 5] = (value >> 40) & 0xFF;
  snapshot->data[snapshot->position + 6] = (value >> 48) & 0xFF;
  snapshot->data[snapshot->position + 7] = (value >> 56) & 0xFF;
  snapshot->position += 8;

  return FROTH_OK;
}

static void patch_u32(froth_snapshot_buffer_t *snapshot,
                      froth_cell_u_t position, uint32_t value) {
  froth_cell_u_t saved_position = snapshot->position;

  snapshot->position = position;
  (void)emit_u32(snapshot, value);
  snapshot->position = saved_position;
}

static froth_error_t emit_bytes(froth_snapshot_buffer_t *snapshot,
                                const uint8_t *data, froth_cell_u_t size) {
  if (snapshot->position + size > FROTH_SNAPSHOT_MAX_BYTES) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  memcpy(&snapshot->data[snapshot->position], data, size);
  snapshot->position += size;

  return FROTH_OK;
}

static froth_error_t emit_cell(froth_snapshot_buffer_t *snapshot,
                               froth_cell_t cell) {
#if FROTH_CELL_SIZE_BITS == 8
  FROTH_TRY(emit_u8(snapshot, (uint8_t)cell));
#elif FROTH_CELL_SIZE_BITS == 16
  FROTH_TRY(emit_u16(snapshot, (uint16_t)cell));
#elif FROTH_CELL_SIZE_BITS == 32
  FROTH_TRY(emit_u32(snapshot, (uint32_t)cell));
#elif FROTH_CELL_SIZE_BITS == 64
  FROTH_TRY(emit_u64(snapshot, (uint64_t)cell));
#endif

  return FROTH_OK;
}

static froth_error_t emit_names(froth_snapshot_buffer_t *snapshot,
                                const name_table_t *name_table) {
  FROTH_TRY(emit_u16(snapshot, name_table->count));

  for (froth_cell_u_t i = 0; i < name_table->count; i++) {
    const name_table_item_t *entry = &name_table->items[i];
    size_t name_length = strlen(entry->name);

    if (name_length > UINT16_MAX) {
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }

    FROTH_TRY(emit_u16(snapshot, (uint16_t)name_length));
    FROTH_TRY(emit_bytes(snapshot, (const uint8_t *)entry->name,
                         (froth_cell_u_t)name_length));
  }

  return FROTH_OK;
}

static froth_error_t emit_pattern_object(froth_snapshot_buffer_t *snapshot,
                                         froth_vm_t *froth_vm,
                                         froth_cell_u_t heap_offset) {
  uint8_t *pattern_data = &froth_vm->heap.data[heap_offset];
  froth_cell_u_t pattern_length = pattern_data[0];

  FROTH_TRY(emit_u32(snapshot, (uint32_t)(pattern_length + 1)));
  FROTH_TRY(emit_u8(snapshot, (uint8_t)pattern_length));

  for (froth_cell_u_t i = 1; i <= pattern_length; i++) {
    FROTH_TRY(emit_u8(snapshot, pattern_data[i]));
  }

  return FROTH_OK;
}

static froth_error_t emit_bstring_object(froth_snapshot_buffer_t *snapshot,
                                         froth_vm_t *froth_vm,
                                         froth_cell_u_t heap_offset) {
  uint8_t *string_data = &froth_vm->heap.data[heap_offset];
  froth_cell_u_t string_length = ((froth_cell_t *)string_data)[0];

  FROTH_TRY(emit_u32(snapshot, (uint32_t)(string_length + 2)));
  FROTH_TRY(emit_u16(snapshot, (uint16_t)string_length));

  for (froth_cell_u_t i = 0; i < string_length; i++) {
    FROTH_TRY(emit_u8(snapshot, string_data[sizeof(froth_cell_t) + i]));
  }

  return FROTH_OK;
}

static froth_error_t emit_quote_token(froth_snapshot_buffer_t *snapshot,
                                      froth_cell_t token,
                                      const name_table_t *name_table,
                                      const object_table_t *object_table) {
  froth_cell_u_t object_id;
  froth_cell_u_t name_id;

  FROTH_TRY(emit_u8(snapshot, (uint8_t)FROTH_CELL_GET_TAG(token)));

  switch (FROTH_CELL_GET_TAG(token)) {
  case FROTH_NUMBER:
    return emit_cell(snapshot, FROTH_CELL_STRIP_TAG(token));

  case FROTH_QUOTE:
  case FROTH_BSTRING:
  case FROTH_CONTRACT:
  case FROTH_PATTERN:
    FROTH_TRY(object_table_find_id(object_table, FROTH_CELL_STRIP_TAG(token),
                                   &object_id));
    return emit_u32(snapshot, (uint32_t)object_id);

  case FROTH_CALL:
  case FROTH_SLOT:
    FROTH_TRY(
        name_table_find_id(name_table, FROTH_CELL_STRIP_TAG(token), &name_id));
    return emit_u16(snapshot, (uint16_t)name_id);

  default:
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
}

static froth_error_t emit_quote_object(froth_snapshot_buffer_t *snapshot,
                                       froth_vm_t *froth_vm,
                                       froth_cell_u_t heap_offset,
                                       const name_table_t *name_table,
                                       const object_table_t *object_table) {
  froth_cell_t *quote_cells = froth_heap_cell_ptr(&froth_vm->heap, heap_offset);
  froth_cell_u_t quote_length = (froth_cell_u_t)quote_cells[0];
  froth_cell_u_t object_length_position = snapshot->position;
  froth_cell_u_t payload_start;

  FROTH_TRY(emit_u32(snapshot, 0));
  payload_start = snapshot->position;

  FROTH_TRY(emit_u16(snapshot, (uint16_t)quote_length));

  for (froth_cell_u_t i = 1; i <= quote_length; i++) {
    FROTH_TRY(
        emit_quote_token(snapshot, quote_cells[i], name_table, object_table));
  }

  patch_u32(snapshot, object_length_position,
            (uint32_t)(snapshot->position - payload_start));

  return FROTH_OK;
}

static froth_error_t emit_objects(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot,
                                  const object_table_t *object_table,
                                  const name_table_t *name_table) {
  FROTH_TRY(emit_u32(snapshot, (uint32_t)object_table->count));

  for (froth_cell_u_t i = 0; i < object_table->count; i++) {
    const object_table_item_t *object = &object_table->items[i];

    FROTH_TRY(emit_u8(snapshot, (uint8_t)object->type));
    FROTH_TRY(emit_u32(snapshot, (uint32_t)object->object_id));

    switch (object->type) {
    case FROTH_PATTERN:
      FROTH_TRY(emit_pattern_object(snapshot, froth_vm, object->heap_offset));
      break;

    case FROTH_BSTRING:
      FROTH_TRY(emit_bstring_object(snapshot, froth_vm, object->heap_offset));
      break;

    case FROTH_QUOTE:
      FROTH_TRY(emit_quote_object(snapshot, froth_vm, object->heap_offset,
                                  name_table, object_table));
      break;

    default:
      break;
    }
  }

  return FROTH_OK;
}

static froth_cell_u_t count_overlay_slots(void) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t overlay_count = 0;

  for (froth_cell_u_t slot_index = 0; slot_index < slot_count; slot_index++) {
    if (froth_slot_is_overlay(slot_index)) {
      overlay_count++;
    }
  }

  return overlay_count;
}

static froth_error_t emit_binding_impl(froth_snapshot_buffer_t *snapshot,
                                       froth_cell_t slot_impl,
                                       const name_table_t *name_table,
                                       const object_table_t *object_table) {
  uint8_t impl_kind = (uint8_t)FROTH_CELL_GET_TAG(slot_impl);

  FROTH_TRY(emit_u8(snapshot, impl_kind));

  switch (impl_kind) {
  case FROTH_NUMBER:
    return emit_cell(snapshot, FROTH_CELL_STRIP_TAG(slot_impl));

  case FROTH_QUOTE:
  case FROTH_PATTERN:
  case FROTH_BSTRING:
  case FROTH_CONTRACT: {
    froth_cell_u_t object_id;
    FROTH_TRY(object_table_find_id(
        object_table, FROTH_CELL_STRIP_TAG(slot_impl), &object_id));
    return emit_u32(snapshot, (uint32_t)object_id);
  }

  case FROTH_SLOT: {
    froth_cell_u_t name_id;
    FROTH_TRY(name_table_find_id(name_table, FROTH_CELL_STRIP_TAG(slot_impl),
                                 &name_id));
    return emit_u16(snapshot, (uint16_t)name_id);
  }

  default:
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
}

static froth_error_t emit_bindings(froth_snapshot_buffer_t *snapshot,
                                   const name_table_t *name_table,
                                   const object_table_t *object_table) {
  froth_cell_u_t slot_count = froth_slot_count();

  FROTH_TRY(emit_u32(snapshot, (uint32_t)count_overlay_slots()));

  for (froth_cell_u_t slot_index = 0; slot_index < slot_count; slot_index++) {
    froth_cell_t slot_impl;
    froth_cell_u_t name_id;

    if (!froth_slot_is_overlay(slot_index)) {
      continue;
    }

    FROTH_TRY(froth_slot_get_impl(slot_index, &slot_impl));
    FROTH_TRY(name_table_find_id(name_table, slot_index, &name_id));

    FROTH_TRY(emit_u16(snapshot, (uint16_t)name_id));
    FROTH_TRY(emit_binding_impl(snapshot, slot_impl, name_table, object_table));

    FROTH_TRY(emit_u32(snapshot, 0xFFFFFFFF));
    FROTH_TRY(emit_u16(snapshot, 0));
    FROTH_TRY(emit_u16(snapshot, 0));
  }

  return FROTH_OK;
}

static froth_error_t froth_snapshot_write_payload(
    froth_vm_t *froth_vm, froth_snapshot_buffer_t *snapshot,
    const name_table_t *name_table, const object_table_t *object_table) {
  FROTH_TRY(emit_names(snapshot, name_table));
  FROTH_TRY(emit_objects(froth_vm, snapshot, object_table, name_table));
  FROTH_TRY(emit_bindings(snapshot, name_table, object_table));

  return FROTH_OK;
}

froth_error_t froth_snapshot_save(froth_vm_t *froth_vm,
                                  froth_snapshot_buffer_t *snapshot,
                                  froth_snapshot_workspace_t *ws) {
  memset(&ws->names, 0, sizeof(ws->names));
  memset(&ws->objects, 0, sizeof(ws->objects));

  FROTH_TRY(collect_snapshot_dependencies(froth_vm, &ws->names, &ws->objects));

  snapshot->position = 0;
  FROTH_TRY(froth_snapshot_write_payload(froth_vm, snapshot, &ws->names,
                                         &ws->objects));

  return FROTH_OK;
}
