#include "froth_executor.h"
#include "froth_slot_table.h"
#include "froth_stack.h"
#include "platform.h"

/*
 * FROTH_REENTRY_DEPTH_MAX limits how many times froth_execute_quote can
 * be re-entered from C code (via primitives like while, catch, call).
 *
 * This is NOT the same as the Froth call depth. Word-calls-word through
 * FROTH_CALL tags in quotation bodies are handled by the trampoline's
 * CS frames and cost zero C stack. But when a C primitive (while, catch,
 * call, etc.) needs to evaluate a quotation, it calls froth_execute_quote
 * from C, which adds a real C stack frame (~40-60 bytes on ARM).
 *
 * FROTH_CS_CAPACITY (default 256) bounds total Froth nesting depth.
 * FROTH_REENTRY_DEPTH_MAX (default 64) bounds C stack consumption.
 * On ESP32 with an 8KB task stack, 64 re-entries is ~3-4KB, leaving
 * room for primitives, FFI callbacks, and platform code.
 */
#ifndef FROTH_REENTRY_DEPTH_MAX
#define FROTH_REENTRY_DEPTH_MAX 64
#endif

/* Push a frame onto the CS. Returns FROTH_ERROR_CALL_DEPTH on overflow. */
static froth_error_t cs_push(froth_cs_t *cs, froth_cell_u_t quote_offset,
                             froth_cell_u_t ip) {
  if (cs->pointer >= cs->capacity)
    return FROTH_ERROR_CALL_DEPTH;
  cs->data[cs->pointer++] = (froth_cs_frame_t){quote_offset, ip};
  return FROTH_OK;
}

/* Look up a slot and invoke whatever's in it — prim or quotation.
 * If the slot holds a non-quote value, push it to DS.
 * Called from the evaluator for top-level identifiers and from the
 * `call` primitive. Quotation impls enter the trampoline. */
froth_error_t froth_execute_slot(froth_vm_t *vm, froth_cell_u_t slot_index) {
  vm->last_error_slot = (froth_cell_t)slot_index;

  froth_native_word_t prim;
  if (froth_slot_get_prim(slot_index, &prim) == FROTH_OK) {
    return prim(vm);
  }

  froth_cell_t impl;
  if (froth_slot_get_impl(slot_index, &impl) == FROTH_OK) {
    if (FROTH_CELL_IS_QUOTE(impl)) {
      return froth_execute_quote(vm, impl);
    }
    FROTH_TRY(froth_stack_push(&vm->ds, impl));
    return FROTH_OK;
  }

  return FROTH_ERROR_UNDEFINED_WORD;
}

/* Trampoline executor. Runs a quotation body without C recursion.
 *
 * Each call to froth_execute_quote snapshots the CS pointer (cs_base)
 * and processes only frames above that base. This makes the trampoline
 * re-entrant: primitives like while, catch, and call can invoke it
 * from within a running trampoline, and each invocation uses its own
 * partition of the CS.
 *
 * Two depth limits apply:
 *   - CS capacity: total Froth call depth (trampoline frames + re-entries).
 *     Bounded by FROTH_CS_CAPACITY. Costs no C stack.
 *   - Re-entry depth: how many times this function appears on the C call
 *     stack simultaneously. Bounded by FROTH_REENTRY_DEPTH_MAX. Each
 *     re-entry costs one C stack frame. */
froth_error_t froth_execute_quote(froth_vm_t *vm, froth_cell_t quote_cell) {
  if (vm->trampoline_depth >= FROTH_REENTRY_DEPTH_MAX)
    return FROTH_ERROR_CALL_DEPTH;
  vm->trampoline_depth++;

  froth_cell_u_t cs_base = vm->cs.pointer;
  froth_cell_u_t rs_snapshot = froth_stack_depth(&vm->rs);

  froth_cell_u_t offset = FROTH_CELL_STRIP_TAG(quote_cell);
  froth_error_t err = cs_push(&vm->cs, offset, 1);

  while (vm->cs.pointer > cs_base && err == FROTH_OK) {
    froth_cs_frame_t *frame = &vm->cs.data[vm->cs.pointer - 1];
    froth_cell_t *base = froth_heap_cell_ptr(&vm->heap, frame->quote_offset);
    froth_cell_u_t length = base[0];

    if (frame->ip > length) {
      vm->cs.pointer--;
      continue;
    }

    platform_check_interrupt(vm);
    if (vm->interrupted != 0) {
      vm->interrupted = 0;
      vm->thrown = FROTH_ERROR_PROGRAM_INTERRUPTED;
      err = FROTH_ERROR_THROW;
      break;
    }

    froth_cell_t cell = base[frame->ip++];
    froth_cell_t tag = FROTH_CELL_GET_TAG(cell);

    switch (tag) {
    case FROTH_NUMBER:
    case FROTH_QUOTE:
    case FROTH_SLOT:
    case FROTH_BSTRING:
    case FROTH_PATTERN:
      err = froth_stack_push(&vm->ds, cell);
      break;

    case FROTH_CALL: {
      froth_cell_u_t slot_index = FROTH_CELL_STRIP_TAG(cell);
      vm->last_error_slot = (froth_cell_t)slot_index;

      froth_native_word_t prim;
      if (froth_slot_get_prim(slot_index, &prim) == FROTH_OK) {
        err = prim(vm);
        break;
      }

      froth_cell_t impl;
      if (froth_slot_get_impl(slot_index, &impl) == FROTH_OK) {
        if (FROTH_CELL_IS_QUOTE(impl)) {
          froth_cell_u_t callee_offset = FROTH_CELL_STRIP_TAG(impl);
          err = cs_push(&vm->cs, callee_offset, 1);
        } else {
          err = froth_stack_push(&vm->ds, impl);
        }
      } else {
        err = FROTH_ERROR_UNDEFINED_WORD;
      }
      break;
    }

    default:
      err = FROTH_ERROR_TYPE_MISMATCH;
      break;
    }
  }

  vm->cs.pointer = cs_base;
  vm->trampoline_depth--;

  if (err == FROTH_OK && froth_stack_depth(&vm->rs) != rs_snapshot) {
    vm->last_error_slot = -1;
    err = FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS;
  }

  return err;
}
