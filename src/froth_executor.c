#include "froth_executor.h"
#include "froth_slot_table.h"
#include "froth_stack.h"

/* Look up a slot and invoke whatever's in it — prim or quotation. */
froth_error_t froth_execute_slot(froth_vm_t* vm, froth_cell_u_t slot_index) {
  froth_primitive_fn_t prim;
  if (froth_slot_get_prim(slot_index, &prim) == FROTH_OK) {
    return prim(vm);
  }

  froth_cell_t impl;
  if (froth_slot_get_impl(slot_index, &impl) == FROTH_OK) {
    return froth_execute_quote(vm, impl);
  }

  return FROTH_ERROR_UNDEFINED_WORD;
}

/* Execute a quotation body from the heap. */
froth_error_t froth_execute_quote(froth_vm_t* vm, froth_cell_t quote_cell) {
  froth_cell_t* heap_cell = froth_heap_cell_ptr(&vm->heap, FROTH_CELL_STRIP_TAG(quote_cell));
  froth_cell_u_t quote_length = heap_cell[0];

  for (froth_cell_u_t i = 1; i <= quote_length; i++) {
    froth_cell_t current_cell = heap_cell[i];

    switch (FROTH_CELL_GET_TAG(current_cell)) {
      case FROTH_NUMBER:
      case FROTH_QUOTE:
      case FROTH_SLOT:
        FROTH_TRY(froth_stack_push(&vm->ds, current_cell));
        break;

      case FROTH_CALL:
        FROTH_TRY(froth_execute_slot(vm, FROTH_CELL_STRIP_TAG(current_cell)));
        break;

      case FROTH_PATTERN:
        FROTH_TRY(froth_stack_push(&vm->ds, current_cell));
        break;

      default:
        return FROTH_ERROR_TYPE_MISMATCH;
    }
  }
  return FROTH_OK;
}
