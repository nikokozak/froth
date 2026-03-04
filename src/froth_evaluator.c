#include "froth_types.h"
#include "froth_vm.h"
#include "froth_reader.h"
#include "froth_stack.h"
#include "froth_slot_table.h"
#include "froth_executor.h"
#include <stddef.h>

/* Resolve a name to a slot index, creating the slot if it doesn't exist yet. */
static froth_error_t resolve_or_create_slot(const char* name, froth_heap_t* heap, froth_cell_u_t* slot_index) {
  froth_error_t err = froth_slot_find_name(name, slot_index);
  if (err == FROTH_ERROR_SLOT_NAME_NOT_FOUND) {
    FROTH_TRY(froth_slot_create(name, heap, slot_index));
  } else if (err != FROTH_OK) {
    return err;
  }
  return FROTH_OK;
}

/* Handle a number token: tag it and push onto DS. */
static froth_error_t froth_evaluator_handle_number(froth_token_t token, froth_vm_t* vm) {
  froth_cell_t cell;
  FROTH_TRY(froth_make_cell(token.number, FROTH_NUMBER, &cell));
  FROTH_TRY(froth_stack_push(&vm->ds, cell));
  return FROTH_OK;
}

/* Handle a bare identifier at top level: resolve/create slot, then invoke. */
static froth_error_t froth_evaluator_handle_identifier(froth_token_t token, froth_vm_t* vm) {
  froth_cell_u_t slot_index;
  FROTH_TRY(resolve_or_create_slot(token.name, &vm->heap, &slot_index));
  FROTH_TRY(froth_execute_slot(vm, slot_index));
  return FROTH_OK;
}

static froth_error_t froth_evaluator_handle_tick_identifier(froth_token_t token, froth_vm_t* vm, froth_cell_t* output_cell) {
  froth_cell_u_t slot_index;
  FROTH_TRY(resolve_or_create_slot(token.name, &vm->heap, &slot_index));
  FROTH_TRY(froth_make_cell(slot_index, FROTH_SLOT, output_cell));
  return FROTH_OK;
}

/* Count direct body cells in a quotation without consuming the reader.
 * Called after "[" has been consumed. Counts each nested quotation as 1.
 * Saves and restores reader position so the build pass can re-read. */
static froth_cell_u_t count_quote_body(froth_reader_t* reader) {
  froth_reader_t saved = *reader;
  froth_cell_u_t count = 0;
  froth_token_t token;

  while (froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF && token.type != FROTH_TOKEN_CLOSE_BRACKET) {
    count++;
    if (token.type == FROTH_TOKEN_OPEN_BRACKET || token.type == FROTH_TOKEN_OPEN_PAT) {
      // Skip to matching "]" by tracking depth
      froth_cell_u_t depth = 1;
      while (depth > 0 && froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF) {
        if (token.type == FROTH_TOKEN_OPEN_BRACKET)  { depth++; }
        if (token.type == FROTH_TOKEN_OPEN_PAT)      { depth++; } // Also skip pattern bodies
        if (token.type == FROTH_TOKEN_CLOSE_BRACKET) { depth--; }
      }
    }
  }

  *reader = saved;
  return count;
}

static froth_error_t count_and_typecheck_pattern_body(froth_reader_t* reader, froth_cell_u_t* out_count) {
  froth_reader_t saved = *reader;
  froth_cell_u_t count = 0;
  froth_token_t token;

  // First count the pattern body
  while (froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF && token.type != FROTH_TOKEN_CLOSE_BRACKET) {

    switch (token.type) {
      case FROTH_TOKEN_IDENTIFIER:
        if (!(token.name[1] == '\0' && token.name[0] >= 'a' && token.name[0] <= 'z')) {
          *reader = saved;
          return FROTH_ERROR_PATTERN_SYNTAX;
        } 
        break;
      case FROTH_TOKEN_NUMBER:
        if (token.number > 255 || token.number < 0) {
          *reader = saved;
          return FROTH_ERROR_PATTERN_SYNTAX; // Only allow numbers that fit in a byte
        }
        break; // Numbers are fine
      default:
        *reader = saved;
        return FROTH_ERROR_PATTERN_SYNTAX; // Other token types not allowed in pattern body
    }

    count++;
  }

  if (count > FROTH_MAX_PERM_SIZE) {
    *reader = saved;
    return FROTH_ERROR_PATTERN_TOO_LARGE;
  }

  *reader = saved;
  *out_count = count;
  return FROTH_OK;
}

static froth_error_t froth_evaluator_handle_open_pat(froth_reader_t* reader, froth_vm_t* vm, froth_cell_t* output_cell) {
  froth_token_t token;
  froth_cell_u_t heap_offset;
  
  // Count pattern length
  froth_cell_u_t body_count;
  FROTH_TRY(count_and_typecheck_pattern_body(reader, &body_count));

  FROTH_TRY(froth_heap_allocate_bytes(1 + body_count, &vm->heap, &heap_offset)); // Allocate 1 byte for pattern length
  vm->heap.data[heap_offset] = (char)body_count; // Store pattern length in heap
  
  froth_cell_u_t heap_location = heap_offset + 1; // Start writing pattern body immediately after length cell
  while (froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF && token.type != FROTH_TOKEN_CLOSE_BRACKET) {
    if (token.type == FROTH_TOKEN_IDENTIFIER) {

      char var_name = token.name[0];
      char index = var_name - 0x61; // Convert 'a'-'z' to 0-25
      vm->heap.data[heap_location] = index; // Store variable name in heap
      
    } else if (token.type == FROTH_TOKEN_NUMBER) {

      vm->heap.data[heap_location] = (char)(token.number & 0xFF); // Store least significant byte of number in heap\e
      
    }
    heap_location++; // Increment heap location for next pattern element
  }

  FROTH_TRY(froth_make_cell(heap_offset, FROTH_PATTERN, output_cell)); // Create pattern cell with heap offset as payload

  return FROTH_OK;

}

/* Build a quotation from the token stream. Called after "[" has been consumed.
 * Uses two passes: first counts direct body cells, then allocates contiguously
 * and fills in the values. Nested quotations are built after the outer block,
 * so the outer body is always contiguous on the heap.
 *
 * Heap layout: [length] [body_cell_0] [body_cell_1] ... [body_cell_n-1] */
static froth_error_t froth_evaluator_handle_open_bracket(froth_reader_t* reader, froth_vm_t* vm, froth_cell_t* output_cell) {
  froth_token_t token;

  // Pass 1: count direct children
  froth_cell_u_t body_count = count_quote_body(reader);

  // Allocate contiguous block: 1 length cell + body_count body cells
  froth_cell_t* block;
  froth_cell_u_t block_offset;
  FROTH_TRY(froth_heap_allocate_cells(1 + body_count, &vm->heap, &block, &block_offset));

  block[0] = body_count;
  froth_cell_u_t body_index = 0;

  // Pass 2: fill in body cells
  while (froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF && token.type != FROTH_TOKEN_CLOSE_BRACKET) {
    switch (token.type) {
      case FROTH_TOKEN_NUMBER:
        FROTH_TRY(froth_make_cell(token.number, FROTH_NUMBER, &block[1 + body_index]));
        body_index++;
        break;

      case FROTH_TOKEN_IDENTIFIER: {
        froth_cell_u_t slot_index;
        FROTH_TRY(resolve_or_create_slot(token.name, &vm->heap, &slot_index));
        FROTH_TRY(froth_make_cell(slot_index, FROTH_CALL, &block[1 + body_index]));
        body_index++;
        break;
      }

      case FROTH_TOKEN_OPEN_BRACKET: {
        froth_cell_t nested_quote;
        FROTH_TRY(froth_evaluator_handle_open_bracket(reader, vm, &nested_quote));
        block[1 + body_index] = nested_quote;
        body_index++;
        break;
      }

      case FROTH_TOKEN_TICK_IDENTIFIER: {
        FROTH_TRY(froth_evaluator_handle_tick_identifier(token, vm, &block[1 + body_index]));
        body_index++;
        break;
      }

      case FROTH_TOKEN_OPEN_PAT: {
        froth_cell_t pattern_cell;
        FROTH_TRY(froth_evaluator_handle_open_pat(reader, vm, &pattern_cell));
        block[1 + body_index] = pattern_cell;
        body_index++;
        break;
      }

      default:
        break;
    }
  }

  if (token.type == FROTH_TOKEN_CLOSE_BRACKET) {
    FROTH_TRY(froth_make_cell(block_offset, FROTH_QUOTE, output_cell));
    return FROTH_OK;
  }

  return FROTH_ERROR_UNTERMINATED_QUOTATION;
}

/* Top-level evaluator. Reads tokens from input and dispatches each one. */
froth_error_t froth_evaluate_input(const char* input, froth_vm_t* vm) {
  froth_reader_t reader;
  froth_token_t token;

  froth_reader_init(&reader, input);

  while (froth_reader_next_token(&reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF) {
    switch (token.type) {
      case FROTH_TOKEN_NUMBER:
        FROTH_TRY(froth_evaluator_handle_number(token, vm));
        break;
      case FROTH_TOKEN_IDENTIFIER:
        FROTH_TRY(froth_evaluator_handle_identifier(token, vm));
        break;
      case FROTH_TOKEN_OPEN_BRACKET: {
        froth_cell_t quote_cell;
        FROTH_TRY(froth_evaluator_handle_open_bracket(&reader, vm, &quote_cell));
        FROTH_TRY(froth_stack_push(&vm->ds, quote_cell));
        break;
      }
      case FROTH_TOKEN_TICK_IDENTIFIER: {
        froth_cell_t slot_ref_cell;
        FROTH_TRY(froth_evaluator_handle_tick_identifier(token, vm, &slot_ref_cell));
        FROTH_TRY(froth_stack_push(&vm->ds, slot_ref_cell));
        break;
      }
      case FROTH_TOKEN_OPEN_PAT: {
        froth_cell_t pattern_cell;
        FROTH_TRY(froth_evaluator_handle_open_pat(&reader, vm, &pattern_cell));
        FROTH_TRY(froth_stack_push(&vm->ds, pattern_cell));
        break;
      }
      default:
        break;
    }
  }

  return FROTH_OK;
}
