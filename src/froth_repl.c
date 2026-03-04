#include "froth_repl.h"
#include "froth_vm.h"
#include "froth_evaluator.h"
#include "froth_slot_table.h"
#include "platform.h"
#include <stdio.h>
#include <stdbool.h>

static char repl_buffer[FROTH_LINE_BUFFER_SIZE];
static const char* repl_prompt = "froth> ";

/* Emit a null-terminated string through the platform layer. */
static froth_error_t emit_string(const char* str) {
  for (const char* p = str; *p != '\0'; p++) {
    FROTH_TRY(platform_emit((uint8_t)*p));
  }
  return FROTH_OK;
}

/* Convert a cell-sized integer to a decimal string.
 * Returns a pointer to a static buffer (not reentrant). */
static char* format_number(froth_cell_t number) {
  static char buf[32];
  snprintf(buf, sizeof(buf), "%" FROTH_CELL_FORMAT, number);
  return buf;
}

/* Return a human-readable name for an error code. */
static const char* error_name(froth_error_t err) {
  switch (err) {
    case FROTH_OK:                        return "ok";
    case FROTH_ERROR_STACK_OVERFLOW:      return "stack overflow";
    case FROTH_ERROR_STACK_UNDERFLOW:     return "stack underflow";
    case FROTH_ERROR_VALUE_OVERFLOW:      return "value overflow";
    case FROTH_ERROR_IO:                  return "i/o error";
    case FROTH_ERROR_HEAP_OUT_OF_MEMORY:  return "heap out of memory";
    case FROTH_ERROR_SLOT_NAME_NOT_FOUND: return "slot name not found";
    case FROTH_ERROR_SLOT_IMPL_NOT_FOUND: return "undefined word";
    case FROTH_ERROR_SLOT_PRIM_NOT_FOUND: return "undefined word";
    case FROTH_ERROR_SLOT_TABLE_FULL:     return "slot table full";
    case FROTH_ERROR_SLOT_INDEX_EMPTY:    return "slot index empty";
    case FROTH_ERROR_TOKEN_TOO_LONG:      return "token too long";
    case FROTH_ERROR_UNTERMINATED_QUOTATION: return "unterminated quotation";
    case FROTH_ERROR_UNRECOGNIZED_CELL_TYPE: return "unrecognized cell type";
    case FROTH_ERROR_ARGUMENT_TYPE_MISMATCH: return "type mismatch";
    case FROTH_ERROR_DIVISION_BY_ZERO:    return "division by zero";
    case FROTH_ERROR_PATTERN_SYNTAX:      return "invalid pattern";
    case FROTH_ERROR_PATTERN_TOO_LARGE:   return "pattern too large";
    case FROTH_ERROR_UNTERMINATED_COMMENT: return "unterminated comment";
    case FROTH_ERROR_UNEXPECTED_PAREN:  return "unexpected )";
    default:                              return "unknown error";
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
  return emit_string(repl_prompt);
}

static froth_error_t froth_repl_read_line(char* output_buffer) {
  froth_cell_u_t pos = 0;
  while (pos < FROTH_LINE_BUFFER_SIZE - 1) {
    uint8_t byte;
    froth_error_t err = platform_key(&byte);
    if (err != FROTH_OK) { return err; }

    if (byte == '\n') { break; }

    output_buffer[pos++] = byte;
  }
  output_buffer[pos] = '\0';
  return FROTH_OK;
}

/* Format a single cell for display. Numbers show their value,
 * ref types show a type acronym and their payload. Examples:
 *   42        — number
 *   Q:16      — quotation at heap offset 16
 *   S:foo     — slot ref for "foo"
 *   C:bar     — call to "bar"
 *   P:0       — pattern ref (payload only, no name yet)
 *   Str:0     — string ref
 *   Con:0     — contract ref */
static froth_error_t emit_cell(froth_cell_t cell) {
  froth_cell_t payload = FROTH_CELL_STRIP_TAG(cell);

  switch (FROTH_CELL_GET_TAG(cell)) {
    case FROTH_NUMBER:
      return emit_string(format_number(payload));

    case FROTH_QUOTE:
      emit_string("Q:");
      return emit_string(format_number(payload));

    case FROTH_SLOT: {
      const char* name;
      if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK) {
        emit_string("S:");
        return emit_string(name);
      }
      emit_string("S:");
      return emit_string(format_number(payload));
    }

    case FROTH_CALL: {
      const char* name;
      if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK) {
        emit_string("C:");
        return emit_string(name);
      }
      emit_string("C:");
      return emit_string(format_number(payload));
    }

    case FROTH_PATTERN:
      emit_string("P:");
      return emit_string(format_number(payload));

    case FROTH_STRING:
      emit_string("Str:");
      return emit_string(format_number(payload));

    case FROTH_CONTRACT:
      emit_string("Con:");
      return emit_string(format_number(payload));

    default:
      return emit_string("<?>");
  }
}

/* Print the data stack contents in the format: [42 Q:16 S:foo] */
static froth_error_t froth_repl_print_stack(froth_stack_t* stack) {
  froth_cell_u_t depth = froth_stack_depth(stack);

  FROTH_TRY(emit_string("["));

  for (froth_cell_u_t i = 0; i < depth; i++) {
    if (i > 0) { FROTH_TRY(platform_emit((uint8_t)' ')); }
    FROTH_TRY(emit_cell(stack->data[i]));
  }

  FROTH_TRY(emit_string("]\n"));
  return FROTH_OK;
}

froth_error_t froth_repl_start(froth_vm_t* vm) {
  froth_error_t err;
  while (true) {
    FROTH_TRY(froth_repl_print_prompt());

    err = froth_repl_read_line(repl_buffer);
    if (err != FROTH_OK) { return FROTH_OK; } // EOF or I/O — exit cleanly

    if (is_blank(repl_buffer)) { continue; }

    err = froth_evaluate_input(repl_buffer, vm);
    if (err != FROTH_OK) {
      emit_string("error: ");
      emit_string(error_name(err));
      emit_string("\n");
      continue;
    }

    FROTH_TRY(froth_repl_print_stack(&vm->ds));
  }
}
