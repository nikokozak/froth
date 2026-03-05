#include "froth_primitives.h"
#include "froth_executor.h"
#include "froth_vm.h"
#include "froth_stack.h"
#include "platform.h"
#include <stdbool.h>

froth_error_t froth_prim_def(froth_vm_t* froth_vm) {
  froth_cell_t slot_cell, impl_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &impl_cell));

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) { return FROTH_ERROR_TYPE_MISMATCH; } 

  FROTH_TRY(froth_slot_set_impl((froth_cell_u_t)FROTH_CELL_STRIP_TAG(slot_cell), impl_cell));

  return FROTH_OK;
}

froth_error_t froth_prim_get(froth_vm_t* froth_vm) {
  froth_cell_t slot_cell, slot_impl; 

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_slot_get_impl((froth_cell_u_t)FROTH_CELL_STRIP_TAG(slot_cell), &slot_impl));

  FROTH_TRY(froth_stack_push(&froth_vm->ds, slot_impl));

  return FROTH_OK;
}

froth_error_t froth_prim_call(froth_vm_t* froth_vm) {
  froth_cell_t callee_cell;
  
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &callee_cell));
  if (!FROTH_CELL_IS_QUOTE(callee_cell) && !FROTH_CELL_IS_SLOT(callee_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

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
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

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
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

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
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

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
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }
  if (FROTH_CELL_STRIP_TAG(b_cell) == 0) { return FROTH_ERROR_DIVISION_BY_ZERO; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t quotient;
  froth_cell_t remainder;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) / FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &quotient));
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) % FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &remainder));
  
  FROTH_TRY(froth_stack_push(&froth_vm->ds, remainder));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, quotient));

  return FROTH_OK;
}

/* pat ( q -- pattern ): validate quotation of indices, pack into byte-packed PatternRef. */
froth_error_t froth_prim_pat(froth_vm_t* froth_vm) {
  froth_cell_t quote_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &quote_cell));
  if (!FROTH_CELL_IS_QUOTE(quote_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t* body = froth_heap_cell_ptr(&froth_vm->heap, FROTH_CELL_STRIP_TAG(quote_cell));
  froth_cell_t len = body[0];

  if (len > FROTH_MAX_PERM_SIZE) { return FROTH_ERROR_PATTERN_TOO_LARGE; }

  // Validate: every body cell must be a non-negative number that fits in a byte
  for (froth_cell_t i = 0; i < len; i++) {
    froth_cell_t cell = body[1 + i];
    if (!FROTH_CELL_IS_NUMBER(cell)) { return FROTH_ERROR_PATTERN_INVALID; }
    froth_cell_t val = FROTH_CELL_STRIP_TAG(cell);
    if (val < 0 || val > 255) { return FROTH_ERROR_PATTERN_INVALID; }
  }

  // All valid — allocate and pack
  froth_cell_u_t pat_offset;
  FROTH_TRY(froth_heap_allocate_bytes(1 + len, &froth_vm->heap, &pat_offset));
  uint8_t* pat = &froth_vm->heap.data[pat_offset];
  pat[0] = (uint8_t)len;
  for (froth_cell_t i = 0; i < len; i++) {
    pat[1 + i] = (uint8_t)FROTH_CELL_STRIP_TAG(body[1 + i]);
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(pat_offset, FROTH_PATTERN, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));
  return FROTH_OK;
}

/* perm ( window_size pattern -- ): rewrite top window_size stack items according to pattern.
 * Pattern reads TOS-right: pattern[0] = deepest output, pattern[pattern_len-1] = new TOS.
 * Each index selects from the input window where 0 = TOS, 1 = next below, etc. */
froth_error_t froth_prim_perm(froth_vm_t* froth_vm) {
  // Pop pattern
  froth_cell_t pattern_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &pattern_cell));
  if (!FROTH_CELL_IS_PATTERN(pattern_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }
  uint8_t* pattern = &froth_vm->heap.data[FROTH_CELL_STRIP_TAG(pattern_cell)];
  uint8_t pattern_len = pattern[0];

  // Pop window size
  froth_cell_t window_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &window_cell));
  if (!FROTH_CELL_IS_NUMBER(window_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }
  froth_cell_t window_size = FROTH_CELL_STRIP_TAG(window_cell);
  if (window_size < 0 || window_size > FROTH_MAX_PERM_SIZE) { return FROTH_ERROR_PATTERN_INVALID; }
  if ((froth_cell_u_t)window_size > froth_vm->ds.pointer) { return FROTH_ERROR_STACK_UNDERFLOW; }

  // Validate all indices are within the window
  for (uint8_t i = 0; i < pattern_len; i++) {
    if (pattern[1 + i] >= window_size) { return FROTH_ERROR_PATTERN_INVALID; }
  }

  // Snapshot the input window into a fixed-size scratch buffer
  froth_cell_t scratch[FROTH_MAX_PERM_SIZE];
  froth_cell_u_t base = froth_vm->ds.pointer - (froth_cell_u_t)window_size;
  for (froth_cell_u_t i = 0; i < (froth_cell_u_t)window_size; i++) {
    scratch[i] = froth_vm->ds.data[base + i];
  }

  // Remove window_size items, push pattern_len items according to pattern
  froth_vm->ds.pointer = base;
  for (uint8_t i = 0; i < pattern_len; i++) {
    FROTH_TRY(froth_stack_push(&froth_vm->ds, scratch[window_size - 1 - pattern[1 + i]]));
  }

  return FROTH_OK;
}

froth_error_t froth_prim_compare_lt(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) < FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_eq(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) == FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_gt(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) > FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_and(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) & FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_choose(froth_vm_t* froth_vm) {
  froth_cell_t false_case_cell, true_case_cell, condition_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &false_case_cell));
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &true_case_cell));
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &condition_cell));
  if (!FROTH_CELL_IS_NUMBER(condition_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  bool condition = FROTH_CELL_STRIP_TAG(condition_cell) != 0;
  froth_cell_t result = condition ? true_case_cell : false_case_cell;
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_while(froth_vm_t* froth_vm) {
  froth_cell_t body_cell, condition_cell, condition_result_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &body_cell));
  if (!FROTH_CELL_IS_QUOTE(body_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &condition_cell));
  if (!FROTH_CELL_IS_QUOTE(condition_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_u_t stack_depth = froth_vm->ds.pointer;

  for (;;) {
    // Execute condition
    FROTH_TRY(froth_execute_quote(froth_vm, condition_cell));
    // Check data stack is depth + 1 ONLY.
    if (froth_vm->ds.pointer != stack_depth + 1) { return FROTH_ERROR_WHILE_STACK; }
    // Get the condition result and validate it's a number
    FROTH_TRY(froth_stack_pop(&froth_vm->ds, &condition_result_cell));
    if (!FROTH_CELL_IS_NUMBER(condition_result_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }
    
    if (FROTH_CELL_STRIP_TAG(condition_result_cell) == 0) { break; }

    // Execute body
    FROTH_TRY(froth_execute_quote(froth_vm, body_cell));
    // Check no cells have been added to stack beyond the condition result.
    if (froth_vm->ds.pointer != stack_depth) { return FROTH_ERROR_WHILE_STACK; }
  }

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_or(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) | FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_xor(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) ^ FROTH_CELL_STRIP_TAG(b_cell), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_invert(froth_vm_t* froth_vm) {
  froth_cell_t a_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(froth_wrap_payload(~FROTH_CELL_STRIP_TAG(a_cell)), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_shl(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(froth_wrap_payload((froth_cell_u_t)FROTH_CELL_STRIP_TAG(a_cell) << (froth_cell_u_t)FROTH_CELL_STRIP_TAG(b_cell)), FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_shr(froth_vm_t* froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

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

  if (!FROTH_CELL_IS_NUMBER(cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

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

froth_error_t froth_prim_throw(froth_vm_t* froth_vm) {
  froth_cell_t error_code_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &error_code_cell));
  if (!FROTH_CELL_IS_NUMBER(error_code_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  if (FROTH_CELL_STRIP_TAG(error_code_cell) == FROTH_OK) { return FROTH_OK; }

  // Store raw error code
  froth_vm->thrown = FROTH_CELL_STRIP_TAG(error_code_cell);
  return FROTH_ERROR_THROW;
};

froth_error_t froth_prim_catch(froth_vm_t* froth_vm) {
  froth_cell_t body_cell, thrown_cell, return_cell;
  froth_cell_u_t ds_depth, rs_depth, cs_depth;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &body_cell));
  if (!FROTH_CELL_IS_QUOTE(body_cell)) { return FROTH_ERROR_TYPE_MISMATCH; }

  ds_depth = froth_vm->ds.pointer;
  rs_depth = froth_vm->rs.pointer;
  cs_depth = froth_vm->cs.pointer;

  froth_error_t err = froth_execute_quote(froth_vm, body_cell);
  if (err != FROTH_OK) {
    froth_vm->ds.pointer = ds_depth;
    froth_vm->rs.pointer = rs_depth;
    froth_vm->cs.pointer = cs_depth;

    /* For explicit throw, vm->thrown is already set.
     * For other runtime errors, the C error code IS the user-visible code. */
    froth_cell_t error_code = (err == FROTH_ERROR_THROW) ? froth_vm->thrown : (froth_cell_t)err;
    FROTH_TRY(froth_make_cell(error_code, FROTH_NUMBER, &thrown_cell));
    FROTH_TRY(froth_stack_push(&froth_vm->ds, thrown_cell));
    return FROTH_OK;
  }

  FROTH_TRY(froth_make_cell(0, FROTH_NUMBER, &return_cell));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, return_cell));
  return FROTH_OK;
}

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
  { .name = "invert", .prim_word = froth_prim_bitwise_invert },
  { .name = "lshift", .prim_word = froth_prim_bitwise_shl },
  { .name = "rshift", .prim_word = froth_prim_bitwise_shr },
  { .name = "emit", .prim_word = froth_prim_emit },
  { .name = "key", .prim_word = froth_prim_key },
  { .name = "key?", .prim_word = froth_prim_key_ready },
  { .name = "pat", .prim_word = froth_prim_pat },
  { .name = "perm", .prim_word = froth_prim_perm },
  { .name = "choose", .prim_word = froth_prim_choose },
  { .name = "while", .prim_word = froth_prim_while },
  { .name = "catch", .prim_word = froth_prim_catch },
  { .name = "throw", .prim_word = froth_prim_throw },
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
