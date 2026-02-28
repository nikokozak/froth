#pragma once
#include "froth_types.h"

#ifndef FROTH_HEAP_SIZE
  #error "FROTH_HEAP_SIZE is not defined. Please define it to the desired size of the heap in bytes."
#endif

typedef struct {
  uint8_t *data;
  froth_cell_u_t pointer;
} froth_heap_t;

froth_heap_t froth_heap;

/* Return a froth_cell_t pointer into the heap at the given byte offset.
 * This is the single sanctioned way to access cell-sized data in the heap.
 * All byte_offset values must come from froth_heap_allocate_cells. */
static inline froth_cell_t* froth_heap_cell_ptr(froth_heap_t* heap, froth_cell_u_t byte_offset) {
  return (froth_cell_t*)&heap->data[byte_offset];
}

froth_error_t froth_heap_allocate_bytes(froth_cell_u_t size, froth_heap_t* froth_heap, froth_cell_u_t* assigned_heap_location);
froth_error_t froth_heap_allocate_cells(froth_cell_u_t size, froth_heap_t* froth_heap, froth_cell_u_t* assigned_heap_location);
