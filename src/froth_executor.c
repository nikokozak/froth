#include "froth_executor.h"
#include "froth_slot_table.h"
#include "froth_stack.h"
#include "platform.h"

/* Look up a slot and invoke whatever's in it — prim or quotation. 
 * If the slot instead holds a value, (it might, in the 'impl' field), then simply push it. */
froth_error_t froth_execute_slot(froth_vm_t* vm, froth_cell_u_t slot_index) {
  vm->last_error_slot = (froth_cell_t)slot_index;

  froth_native_word_t prim;
  if (froth_slot_get_prim(slot_index, &prim) == FROTH_OK) {
    return prim(vm);
  }

  froth_cell_t impl;
  if (froth_slot_get_impl(slot_index, &impl) == FROTH_OK) {
    // Check to see if impl is a quote - if it is, then execute it.
    if (FROTH_CELL_IS_QUOTE(impl)) { return froth_execute_quote(vm, impl); }
    // Otherwise, just push the impl onto the stack.
    else { FROTH_TRY(froth_stack_push(&vm->ds, impl)); }
    return FROTH_OK;
  }

  return FROTH_ERROR_UNDEFINED_WORD;
}

/* Execute a quotation body from the heap. */
froth_error_t froth_execute_quote(froth_vm_t* vm, froth_cell_t quote_cell) {
  froth_cell_t* heap_cell = froth_heap_cell_ptr(&vm->heap, FROTH_CELL_STRIP_TAG(quote_cell));
  froth_cell_u_t quote_length = heap_cell[0];
  froth_cell_u_t rs_length = froth_stack_depth(&vm->rs); // For RS operator quote balance checks

  for (froth_cell_u_t i = 1; i <= quote_length; i++) {
    platform_check_interrupt(vm);
    if (vm->interrupted != 0) {
      vm->interrupted = 0;
      vm->thrown = FROTH_ERROR_PROGRAM_INTERRUPTED;
      return FROTH_ERROR_THROW; }

    froth_cell_t current_cell = heap_cell[i];

    switch (FROTH_CELL_GET_TAG(current_cell)) {
      case FROTH_NUMBER:
      case FROTH_QUOTE:
      case FROTH_SLOT:
      case FROTH_BSTRING:
      case FROTH_PATTERN:
        FROTH_TRY(froth_stack_push(&vm->ds, current_cell));
        break;

      case FROTH_CALL:
        FROTH_TRY(froth_execute_slot(vm, FROTH_CELL_STRIP_TAG(current_cell)));
        break;

      default:
        return FROTH_ERROR_TYPE_MISMATCH;
    }
  }

  if (froth_stack_depth(&vm->rs) != rs_length) {
    vm->last_error_slot = -1;
    return FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS;
  }

  return FROTH_OK;
}
