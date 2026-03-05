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
    /* Reader/evaluator errors */
    case FROTH_ERROR_TOKEN_TOO_LONG:       return "token too long";
    case FROTH_ERROR_UNTERMINATED_QUOTE:   return "unterminated quotation";
    case FROTH_ERROR_UNTERMINATED_COMMENT: return "unterminated comment";
    case FROTH_ERROR_UNEXPECTED_PAREN:     return "unexpected )";
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

#define REPL_QUOTE_DISPLAY_MAX 8

static froth_error_t emit_cell(froth_cell_t cell, froth_heap_t* heap);

/* Emit a quotation body token in display form (no angle brackets). */
static froth_error_t emit_quote_token(froth_cell_t cell, froth_heap_t* heap) {
  froth_cell_t payload = FROTH_CELL_STRIP_TAG(cell);
  switch (FROTH_CELL_GET_TAG(cell)) {
    case FROTH_CALL: {
      const char* name;
      if (froth_slot_get_name((froth_cell_u_t)payload, &name) == FROTH_OK)
        return emit_string(name);
      return emit_string(format_number(payload));
    }
    case FROTH_SLOT: {
      const char* name;
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
static froth_error_t emit_pattern(froth_cell_t payload, froth_heap_t* heap) {
  uint8_t len = heap->data[payload];
  emit_string("p[");
  for (uint8_t i = 0; i < len; i++) {
    if (i > 0) platform_emit((uint8_t)' ');
    char letter = 'a' + heap->data[payload + 1 + i];
    platform_emit((uint8_t)letter);
  }
  return emit_string("]");
}

static froth_error_t emit_cell(froth_cell_t cell, froth_heap_t* heap) {
  froth_cell_t payload = FROTH_CELL_STRIP_TAG(cell);

  switch (FROTH_CELL_GET_TAG(cell)) {
    case FROTH_NUMBER:
      return emit_string(format_number(payload));

    case FROTH_QUOTE: {
      froth_cell_t* body = froth_heap_cell_ptr(heap, payload);
      froth_cell_t len = body[0];
      if (len > REPL_QUOTE_DISPLAY_MAX) {
        emit_string("<q:");
        emit_string(format_number(len));
        return emit_string(">");
      }
      emit_string("[");
      for (froth_cell_t i = 0; i < len; i++) {
        if (i > 0) platform_emit((uint8_t)' ');
        FROTH_TRY(emit_quote_token(body[1 + i], heap));
      }
      return emit_string("]");
    }

    case FROTH_SLOT: {
      const char* name;
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
      const char* name;
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

    case FROTH_STRING:
      return emit_string("<str>");

    case FROTH_CONTRACT:
      return emit_string("<con>");

    default:
      return emit_string("<?>");
  }
}

static froth_error_t froth_repl_print_stack(froth_stack_t* stack, froth_heap_t* heap) {
  froth_cell_u_t depth = froth_stack_depth(stack);

  FROTH_TRY(emit_string("["));

  for (froth_cell_u_t i = 0; i < depth; i++) {
    if (i > 0) { FROTH_TRY(platform_emit((uint8_t)' ')); }
    FROTH_TRY(emit_cell(stack->data[i], heap));
  }

  FROTH_TRY(emit_string("]\n"));
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

    FROTH_TRY(froth_repl_print_stack(&vm->ds, &vm->heap));
  }
}
