#include "froth_heap.h"

static uint8_t heap_memory[FROTH_HEAP_SIZE];

froth_heap_t froth_heap = {
  .data = heap_memory,
  .pointer = 0
};

froth_error_t froth_heap_allocate_bytes(froth_cell_u_t size, froth_heap_t* heap, froth_cell_u_t* assigned_heap_location) {
  if (heap->pointer + size > FROTH_HEAP_SIZE) { // Not enough space in the heap
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  froth_cell_u_t start_pointer = heap->pointer;
  heap->pointer += size; // Move the pointer

  *assigned_heap_location = start_pointer;
  return FROTH_OK;
}

froth_error_t froth_heap_allocate_cells(froth_cell_u_t size, froth_heap_t* heap, froth_cell_u_t* assigned_heap_location) {
  // Align the pointer to the next multiple of sizeof(froth_cell_t)
  froth_cell_u_t aligned_pointer = (heap->pointer + (sizeof(froth_cell_t) - 1)) & ~((sizeof(froth_cell_t) - 1)); 

  if (aligned_pointer + size * sizeof(froth_cell_t) > FROTH_HEAP_SIZE) { // Not enough space in the heap
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  froth_cell_u_t size_in_bytes = size * sizeof(froth_cell_t);

  froth_cell_u_t start_pointer = aligned_pointer;
  heap->pointer = aligned_pointer + size_in_bytes; // Move the pointer

  *assigned_heap_location = start_pointer;
  return FROTH_OK;
}


