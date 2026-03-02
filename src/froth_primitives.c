#include "froth_primitives.h"
#include "froth_executor.h"
#include "froth_vm.h"
#include "froth_stack.h"
#include "platform.h"
#include <stdbool.h>

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

froth_error_t froth_prim_add(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t a = FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_t b = FROTH_CELL_STRIP_TAG(b_cell);
  froth_cell_t wrapped = froth_wrap_payload((froth_cell_u_t)a + (froth_cell_u_t)b);

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_sub(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t a = FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_t b = FROTH_CELL_STRIP_TAG(b_cell);
  froth_cell_t wrapped = froth_wrap_payload((froth_cell_u_t)a - (froth_cell_u_t)b);

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_mul(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t a = FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_t b = FROTH_CELL_STRIP_TAG(b_cell);
  froth_cell_t wrapped = froth_wrap_payload((froth_cell_u_t)a * (froth_cell_u_t)b);

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_divmod(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }
  if (FROTH_CELL_STRIP_TAG(b_cell) == 0) { return FROTH_ERROR_DIVISION_BY_ZERO; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t quotient;
  froth_cell_t remainder;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) / FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &quotient));
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) % FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &remainder));
  
  FROTH_TRY(froth_stack_push(&froth_vm->ds, quotient));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, remainder));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_lt(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) < FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_eq(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) == FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_gt(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) > FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_and(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) & FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_or(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) | FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_xor(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) ^ FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_invert(froth_vm_t* froth_vm) {
  froth_cell_t a_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(froth_wrap_payload(~FROTH_CELL_STRIP_TAG(a_cell)), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_shl(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(froth_wrap_payload((froth_cell_u_t)FROTH_CELL_STRIP_TAG(a_cell) << (froth_cell_u_t)FROTH_CELL_STRIP_TAG(b_cell)), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_shr(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  froth_cell_u_t a = (froth_cell_u_t)FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_u_t b = (froth_cell_u_t)FROTH_CELL_STRIP_TAG(b_cell);
  const froth_cell_u_t pmask = ((froth_cell_u_t)1 << (FROTH_CELL_SIZE_BITS - 3)) - 1; // Mask for valid payload bits
  froth_cell_t wrapped = froth_wrap_payload((a & pmask) >> b); // Mask 'a' to payload bits before shifting, then wrap

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

// Prints the given cell as a text representation to the output.
froth_error_t froth_prim_emit(froth_vm_t* froth_vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &cell));

  if (!FROTH_CELL_IS_NUMBER(cell)) { return FROTH_ERROR_ARGUMENT_TYPE_MISMATCH; }

  FROTH_TRY(platform_emit(FROTH_CELL_STRIP_TAG(cell) & 0xFF)); // Emit the least significant byte of the number as ASCII

  return FROTH_OK;
};

froth_error_t froth_prim_key(froth_vm_t* froth_vm) {
  uint8_t byte;
  FROTH_TRY(platform_key(&byte));

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(byte, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
};

froth_error_t froth_prim_key_ready(froth_vm_t* froth_vm) {
  bool ready = platform_key_ready();

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(ready ? -1 : 0, FROTH_NUMBER, &result)); // -1 for true, 0 for false
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
};

extern const froth_primitive_t froth_primitives[] = {
  { .name = "def", .prim_word = froth_prim_def },
  { .name = "get", .prim_word = froth_prim_get },
  { .name = "call", .prim_word = froth_prim_call },
  { .name = "+", .prim_word = froth_prim_add },
  { .name = "-", .prim_word = froth_prim_sub },
  { .name = "*", .prim_word = froth_prim_mul },
  { .name = "/mod", .prim_word = froth_prim_divmod },
  { .name = "<", .prim_word = froth_prim_compare_lt },
  { .name = "=", .prim_word = froth_prim_compare_eq },
  { .name = ">", .prim_word = froth_prim_compare_gt },
  { .name = "and", .prim_word = froth_prim_bitwise_and },
  { .name = "or", .prim_word = froth_prim_bitwise_or },
  { .name = "xor", .prim_word = froth_prim_bitwise_xor },
  { .name = "not", .prim_word = froth_prim_bitwise_invert },
  { .name = "lshift", .prim_word = froth_prim_bitwise_shl },
  { .name = "rshift", .prim_word = froth_prim_bitwise_shr },
  { .name = "emit", .prim_word = froth_prim_emit },
  { .name = "key", .prim_word = froth_prim_key },
  { .name = "key?", .prim_word = froth_prim_key_ready },
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
