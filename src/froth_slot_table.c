#include "froth_slot_table.h"
#include <string.h>


froth_slot_t slot_table[FROTH_SLOT_TABLE_SIZE];
static froth_cell_u_t slot_pointer = 0;

static char index_has_slot_assigned(froth_cell_u_t index) {
  return slot_table[index].name != NULL;
}

froth_error_t froth_slot_find_name(const char* name, froth_cell_u_t* found_slot_index) {
  for (froth_cell_u_t ip = 0; ip < slot_pointer; ip++) {
    if (strcmp(slot_table[ip].name, name) == 0) {
      *found_slot_index = ip;
      return FROTH_OK;
    }
  }
  return FROTH_ERROR_SLOT_NAME_NOT_FOUND;
};

froth_error_t froth_slot_create(const char* name, froth_heap_t* heap, froth_cell_u_t* created_slot_index) {
  if (slot_pointer >= FROTH_SLOT_TABLE_SIZE) { return FROTH_ERROR_SLOT_TABLE_FULL; }

  froth_cell_u_t name_heap_location;

  if (froth_heap_allocate_bytes(strlen(name) + 1, heap, &name_heap_location) == FROTH_ERROR_HEAP_OUT_OF_MEMORY) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  char* name_in_heap = (char*)(heap->data + name_heap_location);
  strcpy(name_in_heap, name);

  *created_slot_index = slot_pointer;
  slot_table[slot_pointer++] = (froth_slot_t){ .name = name_in_heap, .impl = 0, .prim = NULL };

  return FROTH_OK;
}

froth_error_t froth_slot_get_impl(froth_cell_u_t slot_index, froth_cell_t* impl) {
  if (!index_has_slot_assigned(slot_index)) { return FROTH_ERROR_SLOT_INDEX_EMPTY; }
  *impl = slot_table[slot_index].impl;
  return FROTH_OK;
};

froth_error_t froth_slot_get_prim(froth_cell_u_t slot_index, froth_primitive_fn_t* prim) {
  if (!index_has_slot_assigned(slot_index)) { return FROTH_ERROR_SLOT_INDEX_EMPTY; }
  *prim = slot_table[slot_index].prim;
  return FROTH_OK;
}

froth_error_t froth_slot_set_impl(froth_cell_u_t slot_index, froth_cell_t impl) {
  if (!index_has_slot_assigned(slot_index)) { return FROTH_ERROR_SLOT_INDEX_EMPTY; }
  slot_table[slot_index].impl = impl;
  return FROTH_OK;
}
froth_error_t froth_slot_set_prim(froth_cell_u_t slot_index, froth_primitive_fn_t prim) {
  if (!index_has_slot_assigned(slot_index)) { return FROTH_ERROR_SLOT_INDEX_EMPTY; }
  slot_table[slot_index].prim = prim;
  return FROTH_OK;
}
