#pragma once

#include "froth_types.h"
#include "froth_heap.h"

#ifndef FROTH_SLOT_TABLE_SIZE
  #error "FROTH_SLOT_TABLE_SIZE is not defined. Please define it to the desired size of the slot table."
#endif

typedef froth_error_t (*froth_primitive_fn_t)(void);

typedef struct {
  const char* name;
  froth_cell_t impl; // Pointer into heap (for quoteRef)
  froth_primitive_fn_t prim;
} froth_slot_t;

// find_name should return an erorr if not found, otherwise write to the result pointer.
froth_error_t froth_slot_find_name(const char* name, froth_cell_u_t* found_slot_index);
froth_error_t froth_slot_create(const char* name, froth_heap_t* froth_heap, froth_cell_u_t* created_slot_index);
froth_error_t froth_slot_get_impl(froth_cell_u_t slot_index, froth_cell_t* impl);
froth_error_t froth_slot_get_prim(froth_cell_u_t slot_index, froth_primitive_fn_t* prim);
froth_error_t froth_slot_set_impl(froth_cell_u_t slot_index, froth_cell_t impl);
froth_error_t froth_slot_set_prim(froth_cell_u_t slot_index, froth_primitive_fn_t prim);
froth_error_t froth_slot_get_name(froth_cell_u_t slot_index, const char** name);
