#include "froth_primitives.h"
#include "froth_executor.h"
#include "froth_vm.h"
#include "froth_stack.h"

froth_error_t froth_prim_def(froth_vm_t* froth_vm) {
  froth_cell_t slot_cell, impl_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &impl_cell));
  if (!FROTH_CELL_IS_QUOTE(impl_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; } 

  FROTH_TRY(froth_slot_set_impl((froth_cell_u_t)FROTH_CELL_STRIP_TAG(slot_cell), impl_cell));

  return FROTH_OK;
}

froth_error_t froth_prim_get(froth_vm_t* froth_vm) {
  froth_cell_t slot_cell, slot_impl; 

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_slot_get_impl((froth_cell_u_t)FROTH_CELL_STRIP_TAG(slot_cell), &slot_impl));

  FROTH_TRY(froth_stack_push(&froth_vm->ds, slot_impl));

  return FROTH_OK;
}

froth_error_t froth_prim_call(froth_vm_t* froth_vm) {
  froth_cell_t callee_cell;
  
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &callee_cell));
  if (!FROTH_CELL_IS_QUOTE(callee_cell) && !FROTH_CELL_IS_SLOT(callee_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  if (FROTH_CELL_IS_QUOTE(callee_cell)) {
    FROTH_TRY(froth_execute_quote(froth_vm, callee_cell));
    return FROTH_OK;
  }

  if (FROTH_CELL_IS_SLOT(callee_cell)) {
    FROTH_TRY(froth_execute_slot(froth_vm, FROTH_CELL_STRIP_TAG(callee_cell)));
    return FROTH_OK;
  }

  return FROTH_OK;
}

extern const froth_primitive_t froth_primitives[] = {
  { .name = "def", .prim_word = froth_prim_def },
  { .name = "get", .prim_word = froth_prim_get },
  { .name = "call", .prim_word = froth_prim_call },
};

froth_error_t froth_primitives_register(froth_vm_t* froth_vm) {
  for (froth_cell_u_t i = 0; i < sizeof(froth_primitives) / sizeof(froth_primitive_t); i++) {
    const char* name = froth_primitives[i].name;
    froth_primitive_fn_t prim_fn = froth_primitives[i].prim_word;

    froth_cell_u_t slot_index;
    FROTH_TRY(froth_slot_create(name, &froth_vm->heap, &slot_index));
    FROTH_TRY(froth_slot_set_prim(slot_index, prim_fn));
  }
  return FROTH_OK;
}
