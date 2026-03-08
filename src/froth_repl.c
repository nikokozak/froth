#include "froth_repl.h"
#include "froth_vm.h"
#include "froth_evaluator.h"
#include "froth_slot_table.h"
#include "froth_primitives.h"
#include "platform.h"
#include "froth_fmt.h"
#include <stdio.h>
#include <stdbool.h>

static char repl_buffer[FROTH_LINE_BUFFER_SIZE];
static const char* repl_prompt = "froth> ";

/* Return a human-readable name for an error code. */
static const char* error_name(froth_error_t err) {
  switch (err) {
    case FROTH_OK:                         return "ok";
    /* Runtime errors */
    case FROTH_ERROR_STACK_OVERFLOW:       return "stack overflow";
    case FROTH_ERROR_STACK_UNDERFLOW:      return "stack underflow";
    case FROTH_ERROR_TYPE_MISMATCH:        return "type mismatch";
    case FROTH_ERROR_UNDEFINED_WORD:       return "undefined word";
    case FROTH_ERROR_DIVISION_BY_ZERO:     return "division by zero";
    case FROTH_ERROR_HEAP_OUT_OF_MEMORY:   return "heap out of memory";
    case FROTH_ERROR_PATTERN_INVALID:      return "invalid pattern";
    case FROTH_ERROR_PATTERN_TOO_LARGE:    return "pattern too large";
    case FROTH_ERROR_IO:                   return "i/o error";
    case FROTH_ERROR_NOCATCH:              return "throw with no catch";
    case FROTH_ERROR_WHILE_STACK:          return "while: stack discipline violation";
    case FROTH_ERROR_VALUE_OVERFLOW:       return "value overflow";
    case FROTH_ERROR_BOUNDS:               return "index out of bounds";
    case FROTH_ERROR_PROGRAM_INTERRUPTED:          return "interrupted";
    case FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS: return "unbalanced return stack";
    /* Reader/evaluator errors */
    case FROTH_ERROR_TOKEN_TOO_LONG:       return "token too long";
    case FROTH_ERROR_UNTERMINATED_QUOTE:   return "unterminated quotation";
    case FROTH_ERROR_UNTERMINATED_COMMENT: return "unterminated comment";
    case FROTH_ERROR_UNEXPECTED_PAREN:     return "unexpected )";
    case FROTH_ERROR_BSTRING_TOO_LONG:     return "string too long";
    case FROTH_ERROR_UNTERMINATED_STRING:  return "unterminated string";
    case FROTH_ERROR_INVALID_ESCAPE:       return "invalid escape sequence";
    /* Internal */
    case FROTH_ERROR_THROW:                return "unhandled throw";
    default:                               return "unknown error";
  }
}

/* Return true if the buffer contains only whitespace or is empty. */
static bool is_blank(const char* str) {
  for (const char* p = str; *p != '\0'; p++) {
    if (*p != ' ' && *p != '\t' && *p != '\r') { return false; }
  }
  return true;
}

static froth_error_t froth_repl_print_prompt(void) {
  FROTH_TRY(emit_string(repl_prompt));
  return FROTH_OK;
}

static froth_error_t froth_repl_read_line(char* output_buffer) {
  froth_cell_u_t pos = 0;
  while (pos < FROTH_LINE_BUFFER_SIZE - 1) {
    uint8_t byte;
    froth_error_t err = platform_key(&byte);
    if (err != FROTH_OK) { return err; }

    if (byte == '\b' || byte == 127) { // Handle backspace (127 is DEL, which some terminals send for backspace)
      if (pos > 0) {
        pos--;
        FROTH_TRY(platform_emit('\b')); // Move cursor back
        FROTH_TRY(platform_emit(' '));  // Erase the character
        FROTH_TRY(platform_emit('\b')); // Move cursor back again
      }
      continue;
    }

    if (byte == '\n') { break; }

    output_buffer[pos++] = byte;
  }
  output_buffer[pos] = '\0';
  return FROTH_OK;
}

froth_error_t froth_repl_start(froth_vm_t* vm) {
  froth_error_t err;
  while (true) {
    FROTH_TRY(froth_repl_print_prompt());

    err = froth_repl_read_line(repl_buffer);
    if (err != FROTH_OK) { return FROTH_OK; }

    froth_cell_u_t ds_snapshot = vm->ds.pointer; // For error recovery
    froth_cell_u_t rs_snapshot = vm->rs.pointer;

    if (is_blank(repl_buffer)) { continue; }

    vm->last_error_slot = -1;
    err = froth_evaluate_input(repl_buffer, vm);
    if (err != FROTH_OK) {
      /* For explicit throw, vm->thrown has the user's code.
       * For runtime errors, the C error code is the user-visible code. */
      froth_cell_t code = (err == FROTH_ERROR_THROW) ? vm->thrown : (froth_cell_t)err;

      emit_string("error(");
      emit_string(format_number(code));
      emit_string("): ");
      emit_string(error_name((froth_error_t)code));
      if (vm->last_error_slot >= 0) {
        const char* name;
        if (froth_slot_get_name((froth_cell_u_t)vm->last_error_slot, &name) == FROTH_OK) {
          emit_string(" in \"");
          emit_string(name);
          emit_string("\"");
        }
      }
      emit_string("\n");

      vm->ds.pointer = ds_snapshot;
      vm->rs.pointer = rs_snapshot;

      continue;
    }

    FROTH_TRY(froth_prim_dots(vm)); // Print the entire stack after each successful evaluation
  }
}
