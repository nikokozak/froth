#include "froth_repl.h"
#include "froth_evaluator.h"
#include "froth_fmt.h"
#include "froth_primitives.h"
#include "froth_slot_table.h"
#include "froth_vm.h"
#include "platform.h"
#include <stdbool.h>

static char repl_buffer[FROTH_LINE_BUFFER_SIZE];
static froth_cell_u_t buf_pos = 0;
static froth_cell_u_t line_start = 0;
static int bracket_depth = 0, paren_depth = 0, in_string = 0;
static const char *prompt_cont = ".. ";

/* Return a human-readable name for an error code. */
static const char *error_name(froth_error_t err) {
  switch (err) {
  case FROTH_OK:
    return "ok";
  /* Runtime errors */
  case FROTH_ERROR_STACK_OVERFLOW:
    return "stack overflow";
  case FROTH_ERROR_STACK_UNDERFLOW:
    return "stack underflow";
  case FROTH_ERROR_TYPE_MISMATCH:
    return "type mismatch";
  case FROTH_ERROR_UNDEFINED_WORD:
    return "undefined word";
  case FROTH_ERROR_DIVISION_BY_ZERO:
    return "division by zero";
  case FROTH_ERROR_HEAP_OUT_OF_MEMORY:
    return "heap out of memory";
  case FROTH_ERROR_PATTERN_INVALID:
    return "invalid pattern";
  case FROTH_ERROR_PATTERN_TOO_LARGE:
    return "pattern too large";
  case FROTH_ERROR_IO:
    return "i/o error";
  case FROTH_ERROR_NOCATCH:
    return "throw with no catch";
  case FROTH_ERROR_WHILE_STACK:
    return "while: stack discipline violation";
  case FROTH_ERROR_VALUE_OVERFLOW:
    return "value overflow";
  case FROTH_ERROR_BOUNDS:
    return "index out of bounds";
  case FROTH_ERROR_PROGRAM_INTERRUPTED:
    return "interrupted";
  case FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS:
    return "unbalanced return stack";
  case FROTH_ERROR_SLOT_TABLE_FULL:
    return "slot table full";
  case FROTH_ERROR_REDEF_PRIMITIVE:
    return "cannot redefine primitive";
  case FROTH_ERROR_CALL_DEPTH:
    return "call depth exceeded";
  case FROTH_ERROR_NO_MARK:
    return "no mark set";
  /* Reader/evaluator errors */
  case FROTH_ERROR_TOKEN_TOO_LONG:
    return "token too long";
  case FROTH_ERROR_UNTERMINATED_QUOTE:
    return "unterminated quotation";
  case FROTH_ERROR_UNTERMINATED_COMMENT:
    return "unterminated comment";
  case FROTH_ERROR_UNEXPECTED_PAREN:
    return "unexpected )";
  case FROTH_ERROR_BSTRING_TOO_LONG:
    return "string too long";
  case FROTH_ERROR_UNTERMINATED_STRING:
    return "unterminated string";
  case FROTH_ERROR_INVALID_ESCAPE:
    return "invalid escape sequence";
  case FROTH_ERROR_UNEXPECTED_BRACKET:
    return "unexpected ]";
  /* Snapshot errors */
  case FROTH_ERROR_SNAPSHOT_OVERFLOW:
    return "snapshot buffer overflow";
  case FROTH_ERROR_SNAPSHOT_FORMAT:
    return "snapshot format error";
  case FROTH_ERROR_SNAPSHOT_UNRESOLVED:
    return "snapshot unresolved reference";
  case FROTH_ERROR_SNAPSHOT_BAD_CRC:
    return "snapshot CRC mismatch";
  case FROTH_ERROR_SNAPSHOT_INCOMPAT:
    return "snapshot incompatible ABI";
  case FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT:
    return "no saved snapshot";
  case FROTH_ERROR_SNAPSHOT_BAD_NAME:
    return "snapshot name too long";
  /* Link errors */
  case FROTH_ERROR_LINK_OVERFLOW:
    return "link buffer overflow";
  case FROTH_ERROR_LINK_COBS_DECODE:
    return "link COBS decode error";
  case FROTH_ERROR_LINK_BAD_MAGIC:
    return "link bad magic";
  case FROTH_ERROR_LINK_BAD_VERSION:
    return "link bad version";
  case FROTH_ERROR_LINK_BAD_CRC:
    return "link CRC mismatch";
  case FROTH_ERROR_LINK_TOO_LARGE:
    return "link payload too large";
  case FROTH_ERROR_LINK_UNKNOWN_TYPE:
    return "link unknown message type";
  /* Internal */
  case FROTH_ERROR_THROW:
    return "unhandled throw";
  default:
    return "unknown error";
  }
}

/* Return true if the buffer contains only whitespace or is empty. */
static bool is_blank(const char *str) {
  for (const char *p = str; *p != '\0'; p++) {
    if (*p != ' ' && *p != '\t' && *p != '\r') {
      return false;
    }
  }
  return true;
}

/* ── Depth scanner ─────────────────────────────────────────────────────
 * Lightweight pre-scan of raw text to decide whether an expression is
 * complete.  Tracks bracket nesting (including `:` sugar), paren-comment
 * nesting, and unclosed string literals.  State carries across lines so
 * only the newly appended portion needs to be scanned each time.       */

static int is_scan_delimiter(char c) {
  return c == '[' || c == ']' || c == ';' || c == '(' || c == ')' || c == '"' ||
         c == '\'' || c == '\0' || c == ' ' || c == '\t' || c == '\r' ||
         c == '\n';
}

static void scan_line_depth(const char *text, int *bracket_depth,
                            int *paren_depth, int *in_string) {
  int i = 0;

  while (text[i] != '\0') {
    /* Inside an unclosed string from a previous line */
    if (*in_string) {
      if (text[i] == '\\' && text[i + 1] != '\0') {
        i += 2;
        continue;
      }
      if (text[i] == '"') {
        *in_string = 0;
        i++;
        continue;
      }
      i++;
      continue;
    }

    /* Inside an unclosed paren comment from a previous line */
    if (*paren_depth > 0) {
      if (text[i] == '(') {
        (*paren_depth)++;
      } else if (text[i] == ')') {
        (*paren_depth)--;
      }
      i++;
      continue;
    }

    char c = text[i];

    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      i++;
      continue;
    }

    /* Line comment — skip to newline or end */
    if (c == '\\') {
      while (text[i] != '\0' && text[i] != '\n') {
        i++;
      }
      continue;
    }

    /* Paren comment */
    if (c == '(') {
      *paren_depth = 1;
      i++;
      while (text[i] != '\0' && *paren_depth > 0) {
        if (text[i] == '(') {
          (*paren_depth)++;
        } else if (text[i] == ')') {
          (*paren_depth)--;
        }
        i++;
      }
      continue;
    }

    /* String literal */
    if (c == '"') {
      i++;
      int closed = 0;
      while (text[i] != '\0') {
        if (text[i] == '\\' && text[i + 1] != '\0') {
          i += 2;
          continue;
        }
        if (text[i] == '"') {
          i++;
          closed = 1;
          break;
        }
        i++;
      }
      if (!closed) {
        *in_string = 1;
      }
      continue;
    }

    /* Brackets */
    if (c == '[') {
      (*bracket_depth)++;
      i++;
      continue;
    }
    if (c == ']' || c == ';') {
      (*bracket_depth)--;
      i++;
      continue;
    }

    /* Tick — skip the quoted word, it doesn't affect depth */
    if (c == '\'') {
      i++;
      while (text[i] != '\0' && !is_scan_delimiter(text[i])) {
        i++;
      }
      continue;
    }

    /* Regular word — check for `:` colon-sugar opener */
    int word_start = i;
    while (text[i] != '\0' && !is_scan_delimiter(text[i])) {
      i++;
    }
    if (i - word_start == 1 && text[word_start] == ':') {
      (*bracket_depth)++;
    }
  }
}

/* ── REPL public interface ─────────────────────────────────────────── */

static void reset_state(void) {
  buf_pos = 0;
  line_start = 0;
  bracket_depth = 0;
  paren_depth = 0;
  in_string = 0;
}

froth_error_t froth_repl_init(froth_vm_t *vm) {
  (void)vm;
  for (int i = 0; i < FROTH_LINE_BUFFER_SIZE; i++) {
    repl_buffer[i] = '\0';
  }
  reset_state();
  return FROTH_OK;
}

froth_error_t froth_repl_evaluate(froth_vm_t *vm) {
  if (!is_blank(repl_buffer)) {
    froth_cell_u_t ds_snapshot = vm->ds.pointer;
    froth_cell_u_t rs_snapshot = vm->rs.pointer;

    vm->last_error_slot = -1;
    froth_error_t err = froth_evaluate_input(repl_buffer, vm);
    if (err != FROTH_OK) {
      froth_cell_t code =
          (err == FROTH_ERROR_THROW) ? vm->thrown : (froth_cell_t)err;

      emit_string("error(");
      emit_string(format_number(code));
      emit_string("): ");
      emit_string(error_name((froth_error_t)code));
      if (vm->last_error_slot >= 0) {
        const char *name;
        if (froth_slot_get_name((froth_cell_u_t)vm->last_error_slot, &name) ==
            FROTH_OK) {
          emit_string(" in \"");
          emit_string(name);
          emit_string("\"");
        }
      }
      emit_string("\n");

      vm->ds.pointer = ds_snapshot;
      vm->rs.pointer = rs_snapshot;
    }
    FROTH_TRY(froth_prim_dots(vm));
  }

  reset_state();
  return FROTH_OK;
}

froth_error_t froth_repl_accept_byte(froth_vm_t *vm, char byte, int8_t *state) {
  (void)vm;

  if (buf_pos >= FROTH_LINE_BUFFER_SIZE - 1) {
    *state = -1;
    return FROTH_ERROR_BOUNDS;
  }

  if (byte == 0x04) {
    return FROTH_ERROR_IO;
  }

  if (byte == '\b' || byte == 127) {
    if (buf_pos > line_start) {
      buf_pos--;
      FROTH_TRY(platform_emit('\b'));
      FROTH_TRY(platform_emit(' '));
      FROTH_TRY(platform_emit('\b'));
    }
    *state = 0;
    return FROTH_OK;
  }

  if (byte == '\n') {
    FROTH_TRY(platform_emit('\n'));
    repl_buffer[buf_pos] = '\0';

    scan_line_depth(repl_buffer + line_start, &bracket_depth, &paren_depth,
                    &in_string);

    if (bracket_depth > 0 || paren_depth > 0 || in_string) {
      repl_buffer[buf_pos++] = '\n';
      line_start = buf_pos;
      FROTH_TRY(emit_string(prompt_cont));
      *state = 0;
      return FROTH_OK;
    }

    *state = 1;
    return FROTH_OK;
  }

  /* Regular byte */
  repl_buffer[buf_pos++] = byte;
  platform_emit(byte);
  *state = 0;
  return FROTH_OK;
}
