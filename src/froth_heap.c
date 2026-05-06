#include "froth_heap.h"
#include <stddef.h>

froth_error_t froth_heap_allocate_bytes(froth_cell_u_t size,
                                        froth_heap_t *heap,
                                        froth_cell_u_t *assigned_heap_location) {
  if (size > FROTH_HEAP_SIZE || heap->pointer > FROTH_HEAP_SIZE - size) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  froth_cell_u_t start_pointer = heap->pointer;
  heap->pointer += size;
  if (heap->pointer > heap->high_water) {
    heap->high_water = heap->pointer;
  }

  *assigned_heap_location = start_pointer;
  return FROTH_OK;
}

froth_error_t froth_heap_allocate_cells(froth_cell_u_t count, froth_heap_t *heap,
                                        froth_cell_t **cells_out,
                                        froth_cell_u_t *byte_offset_out) {
  froth_cell_u_t alignment = sizeof(froth_cell_t) - 1u;
  froth_cell_u_t aligned_pointer = (heap->pointer + alignment) & ~alignment;
  froth_cell_u_t requested_bytes;

  if (count > FROTH_HEAP_SIZE / sizeof(froth_cell_t) ||
      aligned_pointer > FROTH_HEAP_SIZE) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  requested_bytes = count * sizeof(froth_cell_t);
  if (requested_bytes > FROTH_HEAP_SIZE - aligned_pointer) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  heap->pointer = aligned_pointer + count * sizeof(froth_cell_t);
  if (heap->pointer > heap->high_water) {
    heap->high_water = heap->pointer;
  }

  *cells_out = (froth_cell_t *)&heap->data[aligned_pointer];
  if (byte_offset_out != NULL) {
    *byte_offset_out = aligned_pointer;
  }
  return FROTH_OK;
}
