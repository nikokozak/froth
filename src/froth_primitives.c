#include "froth_primitives.h"
#include "froth_executor.h"
#include "froth_fmt.h"
#include "froth_heap.h"
#include "froth_slot_table.h"
#include "froth_stack.h"
#include "froth_types.h"
#include "froth_vm.h"
#include "platform.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

froth_error_t froth_prim_def(froth_vm_t *froth_vm) {
  froth_cell_t slot_cell, impl_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &impl_cell));

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_u_t slot_index = (froth_cell_u_t)FROTH_CELL_STRIP_TAG(slot_cell);

  /* Reject redefinition of slots that have a C primitive bound. */
  froth_native_word_t existing_prim;
  if (froth_slot_get_prim(slot_index, &existing_prim) == FROTH_OK) {
    return FROTH_ERROR_REDEF_PRIMITIVE;
  }

  FROTH_TRY(froth_slot_set_impl(slot_index, impl_cell));
  FROTH_TRY(
      froth_slot_set_overlay(slot_index, froth_vm->boot_complete ? 1 : 0));

  return FROTH_OK;
}

froth_error_t froth_prim_get(froth_vm_t *froth_vm) {
  froth_cell_t slot_cell, slot_impl;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_slot_get_impl((froth_cell_u_t)FROTH_CELL_STRIP_TAG(slot_cell),
                                &slot_impl));

  FROTH_TRY(froth_stack_push(&froth_vm->ds, slot_impl));

  return FROTH_OK;
}

froth_error_t froth_prim_call(froth_vm_t *froth_vm) {
  froth_cell_t callee_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &callee_cell));
  if (!FROTH_CELL_IS_QUOTE(callee_cell) && !FROTH_CELL_IS_SLOT(callee_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

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

froth_error_t froth_prim_add(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t a = FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_t b = FROTH_CELL_STRIP_TAG(b_cell);
  froth_cell_t wrapped =
      froth_wrap_payload((froth_cell_u_t)a + (froth_cell_u_t)b);

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_sub(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t a = FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_t b = FROTH_CELL_STRIP_TAG(b_cell);
  froth_cell_t wrapped =
      froth_wrap_payload((froth_cell_u_t)a - (froth_cell_u_t)b);

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_mul(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t a = FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_t b = FROTH_CELL_STRIP_TAG(b_cell);
  froth_cell_t wrapped =
      froth_wrap_payload((froth_cell_u_t)a * (froth_cell_u_t)b);

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_divmod(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  if (FROTH_CELL_STRIP_TAG(b_cell) == 0) {
    return FROTH_ERROR_DIVISION_BY_ZERO;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t quotient;
  froth_cell_t remainder;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) /
                                FROTH_CELL_STRIP_TAG(b_cell),
                            FROTH_NUMBER, &quotient));
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) %
                                FROTH_CELL_STRIP_TAG(b_cell),
                            FROTH_NUMBER, &remainder));

  FROTH_TRY(froth_stack_push(&froth_vm->ds, remainder));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, quotient));

  return FROTH_OK;
}

/* pat ( q -- pattern ): validate quotation of indices, pack into byte-packed
 * PatternRef. */
froth_error_t froth_prim_pat(froth_vm_t *froth_vm) {
  froth_cell_t quote_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &quote_cell));
  if (!FROTH_CELL_IS_QUOTE(quote_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t *body =
      froth_heap_cell_ptr(&froth_vm->heap, FROTH_CELL_STRIP_TAG(quote_cell));
  froth_cell_t len = body[0];

  if (len > FROTH_MAX_PERM_SIZE) {
    return FROTH_ERROR_PATTERN_TOO_LARGE;
  }

  // Validate: every body cell must be a non-negative number that fits in a byte
  for (froth_cell_t i = 0; i < len; i++) {
    froth_cell_t cell = body[1 + i];
    if (!FROTH_CELL_IS_NUMBER(cell)) {
      return FROTH_ERROR_PATTERN_INVALID;
    }
    froth_cell_t val = FROTH_CELL_STRIP_TAG(cell);
    if (val < 0 || val > 255) {
      return FROTH_ERROR_PATTERN_INVALID;
    }
  }

  // All valid — allocate and pack
  froth_cell_u_t pat_offset;
  FROTH_TRY(froth_heap_allocate_bytes(1 + len, &froth_vm->heap, &pat_offset));
  uint8_t *pat = &froth_vm->heap.data[pat_offset];
  pat[0] = (uint8_t)len;
  for (froth_cell_t i = 0; i < len; i++) {
    pat[1 + i] = (uint8_t)FROTH_CELL_STRIP_TAG(body[1 + i]);
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(pat_offset, FROTH_PATTERN, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));
  return FROTH_OK;
}

/* perm ( window_size pattern -- ): rewrite top window_size stack items
 * according to pattern. Pattern reads TOS-right: pattern[0] = deepest output,
 * pattern[pattern_len-1] = new TOS. Each index selects from the input window
 * where 0 = TOS, 1 = next below, etc. */
froth_error_t froth_prim_perm(froth_vm_t *froth_vm) {
  // Pop pattern
  froth_cell_t pattern_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &pattern_cell));
  if (!FROTH_CELL_IS_PATTERN(pattern_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  uint8_t *pattern = &froth_vm->heap.data[FROTH_CELL_STRIP_TAG(pattern_cell)];
  uint8_t pattern_len = pattern[0];

  // Pop window size
  froth_cell_t window_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &window_cell));
  if (!FROTH_CELL_IS_NUMBER(window_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  froth_cell_t window_size = FROTH_CELL_STRIP_TAG(window_cell);
  if (window_size < 0 || window_size > FROTH_MAX_PERM_SIZE) {
    return FROTH_ERROR_PATTERN_INVALID;
  }
  if ((froth_cell_u_t)window_size > froth_vm->ds.pointer) {
    return FROTH_ERROR_STACK_UNDERFLOW;
  }

  // Validate all indices are within the window
  for (uint8_t i = 0; i < pattern_len; i++) {
    if (pattern[1 + i] >= window_size) {
      return FROTH_ERROR_PATTERN_INVALID;
    }
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
    FROTH_TRY(froth_stack_push(&froth_vm->ds,
                               scratch[window_size - 1 - pattern[1 + i]]));
  }

  return FROTH_OK;
}

froth_error_t froth_prim_compare_lt(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(
      FROTH_CELL_STRIP_TAG(a_cell) < FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0,
      FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_eq(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(
      FROTH_CELL_STRIP_TAG(a_cell) == FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0,
      FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_compare_gt(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(
      FROTH_CELL_STRIP_TAG(a_cell) > FROTH_CELL_STRIP_TAG(b_cell) ? -1 : 0,
      FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_and(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) &
                                FROTH_CELL_STRIP_TAG(b_cell),
                            FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_choose(froth_vm_t *froth_vm) {
  froth_cell_t false_case_cell, true_case_cell, condition_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &false_case_cell));
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &true_case_cell));
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &condition_cell));
  if (!FROTH_CELL_IS_NUMBER(condition_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  bool condition = FROTH_CELL_STRIP_TAG(condition_cell) != 0;
  froth_cell_t result = condition ? true_case_cell : false_case_cell;
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_while(froth_vm_t *froth_vm) {
  froth_cell_t body_cell, condition_cell, condition_result_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &body_cell));
  if (!FROTH_CELL_IS_QUOTE(body_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &condition_cell));
  if (!FROTH_CELL_IS_QUOTE(condition_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_u_t stack_depth = froth_vm->ds.pointer;

  for (;;) {
    // Check for interrupt flag here.
    if (froth_vm->interrupted) {
      froth_vm->interrupted =
          0; // Clear the flag so that if the user re-issues the command, it
             // will run instead of immediately interrupting again.
      froth_vm->thrown = FROTH_ERROR_PROGRAM_INTERRUPTED;
      return FROTH_ERROR_THROW;
    }
    // Execute condition
    FROTH_TRY(froth_execute_quote(froth_vm, condition_cell));
    // Check data stack is depth + 1 ONLY.
    if (froth_vm->ds.pointer != stack_depth + 1) {
      return FROTH_ERROR_WHILE_STACK;
    }
    // Get the condition result and validate it's a number
    FROTH_TRY(froth_stack_pop(&froth_vm->ds, &condition_result_cell));
    if (!FROTH_CELL_IS_NUMBER(condition_result_cell)) {
      return FROTH_ERROR_TYPE_MISMATCH;
    }

    if (FROTH_CELL_STRIP_TAG(condition_result_cell) == 0) {
      break;
    }

    // Execute body
    FROTH_TRY(froth_execute_quote(froth_vm, body_cell));
    // Check no cells have been added to stack beyond the condition result.
    if (froth_vm->ds.pointer != stack_depth) {
      return FROTH_ERROR_WHILE_STACK;
    }
  }

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_or(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) |
                                FROTH_CELL_STRIP_TAG(b_cell),
                            FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_xor(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(a_cell) ^
                                FROTH_CELL_STRIP_TAG(b_cell),
                            FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_invert(froth_vm_t *froth_vm) {
  froth_cell_t a_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(froth_wrap_payload(~FROTH_CELL_STRIP_TAG(a_cell)),
                            FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_shl(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(
      froth_wrap_payload((froth_cell_u_t)FROTH_CELL_STRIP_TAG(a_cell)
                         << (froth_cell_u_t)FROTH_CELL_STRIP_TAG(b_cell)),
      FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_bitwise_shr(froth_vm_t *froth_vm) {
  froth_cell_t a_cell, b_cell;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &b_cell));
  if (!FROTH_CELL_IS_NUMBER(b_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &a_cell));
  if (!FROTH_CELL_IS_NUMBER(a_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_u_t a = (froth_cell_u_t)FROTH_CELL_STRIP_TAG(a_cell);
  froth_cell_u_t b = (froth_cell_u_t)FROTH_CELL_STRIP_TAG(b_cell);
  const froth_cell_u_t pmask =
      ((froth_cell_u_t)1 << (FROTH_CELL_SIZE_BITS - 3)) -
      1; // Mask for valid payload bits
  froth_cell_t wrapped = froth_wrap_payload(
      (a & pmask) >> b); // Mask 'a' to payload bits before shifting, then wrap

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(wrapped, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

// Prints the given cell as a text representation to the output.
froth_error_t froth_prim_emit(froth_vm_t *froth_vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &cell));

  if (!FROTH_CELL_IS_NUMBER(cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(platform_emit(
      FROTH_CELL_STRIP_TAG(cell) &
      0xFF)); // Emit the least significant byte of the number as ASCII

  return FROTH_OK;
};

froth_error_t froth_prim_key(froth_vm_t *froth_vm) {
  uint8_t byte;
  FROTH_TRY(platform_key(&byte));

  /* On ESP32, platform_key is byte-transparent (no 0x03 interception).
     The key primitive is always in console context, so Ctrl-C should
     interrupt. Set the flag here; the executor's safe-point check will
     fire the interrupt before the next word executes. */
  if (byte == 0x03) {
    froth_vm->interrupted = 1;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(byte, FROTH_NUMBER, &result));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
}

froth_error_t froth_prim_key_ready(froth_vm_t *froth_vm) {
  bool ready = platform_key_ready();

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(ready ? -1 : 0, FROTH_NUMBER,
                            &result)); // -1 for true, 0 for false
  FROTH_TRY(froth_stack_push(&froth_vm->ds, result));

  return FROTH_OK;
};

froth_error_t froth_prim_throw(froth_vm_t *froth_vm) {
  froth_cell_t error_code_cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &error_code_cell));
  if (!FROTH_CELL_IS_NUMBER(error_code_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  if (FROTH_CELL_STRIP_TAG(error_code_cell) == FROTH_OK) {
    return FROTH_OK;
  }

  // Store raw error code
  froth_vm->thrown = FROTH_CELL_STRIP_TAG(error_code_cell);
  return FROTH_ERROR_THROW;
};

froth_error_t froth_prim_catch(froth_vm_t *froth_vm) {
  froth_cell_t body_cell, thrown_cell, return_cell;
  froth_cell_u_t ds_depth, rs_depth, cs_depth;

  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &body_cell));
  if (!FROTH_CELL_IS_QUOTE(body_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

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
    froth_cell_t error_code =
        (err == FROTH_ERROR_THROW) ? froth_vm->thrown : (froth_cell_t)err;
    FROTH_TRY(froth_make_cell(error_code, FROTH_NUMBER, &thrown_cell));
    FROTH_TRY(froth_stack_push(&froth_vm->ds, thrown_cell));
    return FROTH_OK;
  }

  FROTH_TRY(froth_make_cell(0, FROTH_NUMBER, &return_cell));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, return_cell));
  return FROTH_OK;
}
/*------------- END OF CORE PRIMITIVES -------------*/

/*------------------------------------------------------------*
 *------------------------------------------------------------*
 *------------- PRINTING HELPERS AND PRIMITIVES --------------*
 *------------------------------------------------------------*
 *------------------------------------------------------------*/

#define REPL_QUOTE_DISPLAY_MAX 8

static froth_error_t emit_cell(froth_cell_t cell, froth_heap_t *heap);

/* Emit a quotation body token in display form (no angle brackets). */
static froth_error_t emit_quote_token(froth_cell_t cell, froth_heap_t *heap) {
  froth_cell_t payload = FROTH_CELL_STRIP_TAG(cell);
  switch (FROTH_CELL_GET_TAG(cell)) {
  case FROTH_CALL: {
    const char *name;
    if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK)
      return emit_string(name);
    return emit_string(format_number(payload));
  }
  case FROTH_SLOT: {
    const char *name;
    emit_string("'");
    if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK)
      return emit_string(name);
    return emit_string(format_number(payload));
  }
  default:
    return emit_cell(cell, heap);
  }
}

/* Emit a pattern as p[a b c ...]. */
static froth_error_t emit_pattern(froth_cell_t payload, froth_heap_t *heap) {
  uint8_t len = heap->data[payload];
  emit_string("p[");
  for (uint8_t i = 0; i < len; i++) {
    if (i > 0)
      platform_emit((uint8_t)' ');
    char letter = 'a' + heap->data[payload + 1 + i];
    platform_emit((uint8_t)letter);
  }
  return emit_string("]");
}

static froth_error_t emit_cell(froth_cell_t cell, froth_heap_t *heap) {
  froth_cell_t payload = FROTH_CELL_STRIP_TAG(cell);

  switch (FROTH_CELL_GET_TAG(cell)) {
  case FROTH_NUMBER:
    return emit_string(format_number(payload));

  case FROTH_QUOTE: {
    froth_cell_t *body = froth_heap_cell_ptr(heap, payload);
    froth_cell_t len = body[0];
    if (len > REPL_QUOTE_DISPLAY_MAX) {
      emit_string("<q:");
      emit_string(format_number(len));
      return emit_string(">");
    }
    emit_string("[");
    for (froth_cell_t i = 0; i < len; i++) {
      if (i > 0)
        platform_emit((uint8_t)' ');
      FROTH_TRY(emit_quote_token(body[1 + i], heap));
    }
    return emit_string("]");
  }

  case FROTH_SLOT: {
    const char *name;
    if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK) {
      emit_string("<s:");
      emit_string(name);
      return emit_string(">");
    }
    emit_string("<s:");
    emit_string(format_number(payload));
    return emit_string(">");
  }

  case FROTH_CALL: {
    const char *name;
    if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK) {
      emit_string("<c:");
      emit_string(name);
      return emit_string(">");
    }
    emit_string("<c:");
    emit_string(format_number(payload));
    return emit_string(">");
  }

  case FROTH_PATTERN:
    return emit_pattern(payload, heap);

  case FROTH_BSTRING: {
    froth_cell_t len;
    memcpy(&len, heap->data + payload, sizeof(froth_cell_t));
    uint8_t *bstr_data = heap->data + payload + sizeof(froth_cell_t);
    emit_string("\"");
    for (froth_cell_t i = 0; i < len; i++) {
      uint8_t b = bstr_data[i];
      if (b == '\n') {
        FROTH_TRY(emit_string("\\n"));
      } else if (b == '\t') {
        FROTH_TRY(emit_string("\\t"));
      } else if (b == '\r') {
        FROTH_TRY(emit_string("\\r"));
      } else if (b == '"') {
        FROTH_TRY(emit_string("\\\""));
      } else if (b == '\\') {
        FROTH_TRY(emit_string("\\\\"));
      } else if (b < 0x20 || b > 0x7E) {
        char hex[5];
        snprintf(hex, sizeof(hex), "\\x%02X", b);
        FROTH_TRY(emit_string(hex));
      } else {
        FROTH_TRY(platform_emit(b));
      }
    }
    return emit_string("\"");
  }

  case FROTH_CONTRACT:
    return emit_string("<con>");

  default:
    return emit_string("<?>");
  }
}

static froth_error_t print_stack(froth_stack_t *stack, froth_heap_t *heap) {
  froth_cell_u_t depth = froth_stack_depth(stack);

  FROTH_TRY(emit_string("["));

  for (froth_cell_u_t i = 0; i < depth; i++) {
    if (i > 0) {
      FROTH_TRY(platform_emit((uint8_t)' '));
    }
    FROTH_TRY(emit_cell(stack->data[i], heap));
  }

  FROTH_TRY(emit_string("]\n"));
  return FROTH_OK;
}

froth_error_t froth_prim_dot(froth_vm_t *froth_vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &cell));

  FROTH_TRY(emit_cell(
      cell, &froth_vm->heap)); // Emit the cell's text representation to output
  FROTH_TRY(emit_string(" "));

  return FROTH_OK;
};

froth_error_t froth_prim_dots(froth_vm_t *froth_vm) {
  FROTH_TRY(print_stack(&froth_vm->ds, &froth_vm->heap));

  return FROTH_OK;
};

froth_error_t froth_prim_rpush(froth_vm_t *froth_vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->ds, &cell));
  FROTH_TRY(froth_stack_push(&froth_vm->rs, cell));

  return FROTH_OK;
};

froth_error_t froth_prim_rpop(froth_vm_t *froth_vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&froth_vm->rs, &cell));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, cell));

  return FROTH_OK;
};

froth_error_t froth_prim_rpeek(froth_vm_t *froth_vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_peek(&froth_vm->rs, &cell));
  FROTH_TRY(froth_stack_push(&froth_vm->ds, cell));

  return FROTH_OK;
};

froth_error_t froth_prim_words(froth_vm_t *froth_vm) {
  froth_cell_u_t idx = 0;

  for (;;) {
    const char *name;
    froth_error_t err = froth_slot_get_name(idx, &name);

    if (err == FROTH_ERROR_UNDEFINED_WORD) {
      break;
    }

    FROTH_TRY(emit_string(name));
    FROTH_TRY(emit_string(" | "));

    idx++;
  }

  FROTH_TRY(emit_string("\n"));
  return FROTH_OK;
};

/* --- String primitives --------------------------------------------------- */

/* Pop a BString cell, extract length and pointer to byte data on the heap. */
static froth_error_t pop_bstring(froth_vm_t *vm, froth_cell_t *out_len,
                                 uint8_t **out_data) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &cell));
  if (!FROTH_CELL_IS_BSTRING(cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  uint8_t *base = &vm->heap.data[FROTH_CELL_STRIP_TAG(cell)];
  memcpy(out_len, base, sizeof(froth_cell_t));
  *out_data = base + sizeof(froth_cell_t);
  return FROTH_OK;
}

froth_error_t froth_prim_bstring_emit(froth_vm_t *vm) {
  froth_cell_t len;
  uint8_t *data;
  FROTH_TRY(pop_bstring(vm, &len, &data));
  return emit_string((const char *)data);
}

froth_error_t froth_prim_bstring_length(froth_vm_t *vm) {
  froth_cell_t len;
  uint8_t *data;
  FROTH_TRY(pop_bstring(vm, &len, &data));
  froth_cell_t result;
  FROTH_TRY(froth_make_cell(len, FROTH_NUMBER, &result));
  return froth_stack_push(&vm->ds, result);
}

froth_error_t froth_prim_bstring_isequal(froth_vm_t *vm) {
  froth_cell_t len1, len2;
  uint8_t *data1;
  uint8_t *data2;
  FROTH_TRY(pop_bstring(vm, &len2, &data2));
  FROTH_TRY(pop_bstring(vm, &len1, &data1));
  int equal = (len1 == len2) && (memcmp(data1, data2, len1) == 0);
  froth_cell_t result;
  FROTH_TRY(froth_make_cell(equal ? -1 : 0, FROTH_NUMBER, &result));
  return froth_stack_push(&vm->ds, result);
}

froth_error_t froth_prim_bstring_byteat(froth_vm_t *vm) {
  froth_cell_t index_cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &index_cell));
  if (!FROTH_CELL_IS_NUMBER(index_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  froth_cell_t index = FROTH_CELL_STRIP_TAG(index_cell);

  froth_cell_t len;
  uint8_t *data;
  FROTH_TRY(pop_bstring(vm, &len, &data));
  if (index < 0 || index >= len) {
    return FROTH_ERROR_BOUNDS;
  }

  froth_cell_t result;
  FROTH_TRY(froth_make_cell(data[index], FROTH_NUMBER, &result));
  return froth_stack_push(&vm->ds, result);
}

froth_error_t froth_prim_quote_len(froth_vm_t *vm) {
  froth_cell_t quote_cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &quote_cell));
  if (!FROTH_CELL_IS_QUOTE(quote_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  froth_cell_t return_cell;
  froth_cell_t *quote_location =
      froth_heap_cell_ptr(&vm->heap, FROTH_CELL_STRIP_TAG(quote_cell));
  FROTH_TRY(froth_make_cell(quote_location[0], FROTH_NUMBER, &return_cell));

  return froth_stack_push(&vm->ds, return_cell);
}

froth_error_t froth_prim_quote_at(froth_vm_t *vm) {
  froth_cell_t idx, quote_cell, return_cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &idx));
  FROTH_TRY(froth_stack_pop(&vm->ds, &quote_cell));

  if (!FROTH_CELL_IS_NUMBER(idx)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }
  if (!FROTH_CELL_IS_QUOTE(quote_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_t idx_val = FROTH_CELL_STRIP_TAG(idx);
  froth_cell_t *quote_location =
      froth_heap_cell_ptr(&vm->heap, FROTH_CELL_STRIP_TAG(quote_cell));
  froth_cell_u_t quote_length = quote_location[0];
  if (idx_val < 0 || idx_val >= quote_length) {
    return FROTH_ERROR_BOUNDS;
  }

  if (FROTH_CELL_IS_CALL(quote_location[idx_val + 1])) {
    FROTH_TRY(froth_make_cell(FROTH_CELL_STRIP_TAG(quote_location[idx_val + 1]),
                              FROTH_SLOT, &return_cell));
    return froth_stack_push(&vm->ds, return_cell);
  }

  return froth_stack_push(&vm->ds, quote_location[idx_val + 1]);
}

froth_error_t froth_prim_mark(froth_vm_t *vm) {
  vm->mark_offset = vm->heap.pointer;

  return FROTH_OK;
}

froth_error_t froth_prim_release(froth_vm_t *vm) {
  if (vm->mark_offset == (froth_cell_u_t)-1) { // No mark set
    return FROTH_ERROR_NO_MARK;
  }

  vm->heap.pointer = vm->mark_offset;
  vm->mark_offset = (froth_cell_u_t)-1;

  return FROTH_OK;
}

froth_error_t froth_prim_see(froth_vm_t *vm) {
  froth_cell_t slot_cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &slot_cell));
  if (!FROTH_CELL_IS_SLOT(slot_cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  froth_cell_u_t slot_index = FROTH_CELL_STRIP_TAG(slot_cell);
  const char *name;
  FROTH_TRY(froth_slot_get_name(slot_index, &name));

  froth_cell_t impl;
  froth_native_word_t prim = NULL;
  const froth_ffi_entry_t *ffi = NULL;
  int has_impl =
      (froth_slot_get_impl(slot_index, &impl) == FROTH_OK && impl != 0);
  int has_prim =
      (froth_slot_get_prim(slot_index, &prim) == FROTH_OK && prim != NULL);

  if (!has_impl && !has_prim) {
    FROTH_TRY(emit_string(name));
    FROTH_TRY(emit_string(" | <unbound>\n"));
    return FROTH_OK;
  }

  if (has_prim)
    ffi = froth_ffi_find_entry(prim);

  FROTH_TRY(emit_string(name));
  FROTH_TRY(emit_string(" | "));
  if (ffi != NULL) {
    FROTH_TRY(emit_string(ffi->stack_effect));
  }
  FROTH_TRY(emit_string(" | "));
  if (has_impl) {
    FROTH_TRY(emit_cell(impl, &vm->heap));
  } else {
    FROTH_TRY(emit_string("<primitive>"));
  }
  FROTH_TRY(emit_string("\n"));
  if (ffi != NULL && ffi->help != NULL) {
    FROTH_TRY(emit_string(ffi->help));
    FROTH_TRY(emit_string(" | "));
  }
  FROTH_TRY(emit_string(has_prim ? "primitive" : "user-defined"));
  FROTH_TRY(emit_string("\n"));
  return FROTH_OK;
}

froth_error_t froth_prim_dangerous_reset(froth_vm_t *vm) {
  FROTH_TRY(froth_slot_reset_overlay());
  vm->heap.pointer = vm->watermark_heap_offset;

  vm->ds.pointer = 0;
  vm->rs.pointer = 0;
  vm->cs.pointer = 0;

  vm->thrown = FROTH_OK;
  vm->last_error_slot = -1;
  vm->trampoline_depth = 0;
  vm->mark_offset = (froth_cell_u_t)-1;

  return FROTH_ERROR_RESET;
}

froth_error_t froth_prim_info(froth_vm_t *vm) {
  FROTH_TRY(emit_string("Froth v" FROTH_VERSION " | "));
  FROTH_TRY(emit_string(format_number(FROTH_CELL_SIZE_BITS)));
  FROTH_TRY(emit_string("-bit cells\n"));
  FROTH_TRY(emit_string("heap: "));
  FROTH_TRY(emit_string(format_number(vm->heap.pointer)));
  FROTH_TRY(emit_string(" / "));
  FROTH_TRY(emit_string(format_number(FROTH_HEAP_SIZE)));
  FROTH_TRY(emit_string(" bytes used ("));
  FROTH_TRY(
      emit_string(format_number(vm->heap.pointer - vm->watermark_heap_offset)));
  FROTH_TRY(emit_string(" user)\n"));
  FROTH_TRY(emit_string("slots: "));
  FROTH_TRY(emit_string(format_number(froth_slot_count())));
  FROTH_TRY(emit_string(" / "));
  FROTH_TRY(emit_string(format_number(FROTH_SLOT_TABLE_SIZE)));
  return emit_string("\n");
}

const froth_ffi_entry_t froth_primitives[] = {
    /* Core */
    {"def", froth_prim_def, "( 'name value -- )", "Bind value to slot"},
    {"get", froth_prim_get, "( 'name -- value )", "Fetch slot value"},
    {"call", froth_prim_call, "( callable -- )", "Execute quote or slot"},

    /* Arithmetic */
    {"+", froth_prim_add, "( a b -- a+b )", "Add"},
    {"-", froth_prim_sub, "( a b -- a-b )", "Subtract"},
    {"*", froth_prim_mul, "( a b -- a*b )", "Multiply"},
    {"/mod", froth_prim_divmod, "( a b -- rem quot )", "Divide with remainder"},

    /* Comparison */
    {"<", froth_prim_compare_lt, "( a b -- flag )", "Less than"},
    {"=", froth_prim_compare_eq, "( a b -- flag )", "Equal"},
    {">", froth_prim_compare_gt, "( a b -- flag )", "Greater than"},

    /* Bitwise */
    {"and", froth_prim_bitwise_and, "( a b -- a&b )", "Bitwise AND"},
    {"or", froth_prim_bitwise_or, "( a b -- a|b )", "Bitwise OR"},
    {"xor", froth_prim_bitwise_xor, "( a b -- a^b )", "Bitwise XOR"},
    {"invert", froth_prim_bitwise_invert, "( a -- ~a )", "Bitwise NOT"},
    {"lshift", froth_prim_bitwise_shl, "( a n -- a<<n )", "Left shift"},
    {"rshift", froth_prim_bitwise_shr, "( a n -- a>>n )",
     "Logical right shift"},

    /* I/O */
    {"emit", froth_prim_emit, "( char -- )", "Emit low byte as character"},
    {"key", froth_prim_key, "( -- char )", "Read one byte from input"},
    {"key?", froth_prim_key_ready, "( -- flag )", "True if input available"},

    /* Pattern */
    {"pat", froth_prim_pat, "( quote -- pattern )",
     "Compile quotation to pattern"},
    {"perm", froth_prim_perm, "( n pat -- )", "Permute top n stack items"},

    /* Return stack */
    {">r", froth_prim_rpush, "( x -- ) ( R: -- x )",
     "Move TOS to return stack"},
    {"r>", froth_prim_rpop, "( -- x ) ( R: x -- )", "Move top of RS to DS"},
    {"r@", froth_prim_rpeek, "( -- x ) ( R: x -- x )", "Copy top of RS to DS"},

    /* Control flow */
    {"choose", froth_prim_choose, "( flag t f -- t|f )", "Conditional select"},
    {"while", froth_prim_while, "( cond body -- )",
     "Loop while cond yields true"},

    /* Error handling */
    {"catch", froth_prim_catch, "( quote -- code )",
     "Execute quote, catch errors"},
    {"throw", froth_prim_throw, "( code -- )", "Throw error code"},

    /* String */
    {"s.emit", froth_prim_bstring_emit, "( s -- )", "Print string"},
    {"s.len", froth_prim_bstring_length, "( s -- n )", "String byte length"},
    {"s@", froth_prim_bstring_byteat, "( s i -- byte )", "Fetch byte at index"},
    {"s.=", froth_prim_bstring_isequal, "( s1 s2 -- flag )", "String equality"},

    /* Quotation introspection */
    {"q.len", froth_prim_quote_len, "( q -- n )", "Quotation body length"},
    {"q@", froth_prim_quote_at, "( q i -- cell )", "Fetch cell at index"},

    /* Memory */
    {"mark", froth_prim_mark, "( -- )", "Snapshot heap pointer"},
    {"release", froth_prim_release, "( -- )", "Restore heap to last mark"},

    /* Display / introspection */
    {".", froth_prim_dot, "( x -- )", "Print and consume top"},
    {".s", froth_prim_dots, "( -- )", "Print stack"},
    {"words", froth_prim_words, "( -- )", "List all defined words"},
    {"see", froth_prim_see, "( slot -- )", "Display slot definition"},
    {"info", froth_prim_info, "( -- )", "Print system info"},

    /* Reset */
    {"dangerous-reset", froth_prim_dangerous_reset, "( -- )",
     "Wipe overlay state back to stdlib baseline"},

    {0}};
