#include "froth_types.h"
#include "froth_reader.h"
#include "froth_stack.h"
#include "froth_slot_table.h"
#include <stddef.h>

/* Resolve a name to a slot index, creating the slot if it doesn't exist yet.
 * Used by both the top-level evaluator and the quotation builder. */
static froth_error_t resolve_or_create_slot(const char* name, froth_heap_t* heap, froth_cell_u_t* slot_index) {
  froth_error_t err = froth_slot_find_name(name, slot_index);
  if (err == FROTH_ERROR_SLOT_NAME_NOT_FOUND) {
    err = froth_slot_create(name, heap, slot_index);
    if (err != FROTH_OK) { return err; }
  } else if (err != FROTH_OK) {
    return err;
  }
  return FROTH_OK;
}

/* Handle a number token: tag it and push onto DS. */
static froth_error_t froth_evaluator_handle_number(froth_token_t token, froth_stack_t* ds) {
  froth_cell_t cell;
  froth_error_t err;

  err = froth_make_cell(token.number, FROTH_NUMBER, &cell);
  if (err != FROTH_OK) { return err; }

  err = froth_stack_push(ds, cell);
  if (err != FROTH_OK) { return err; }
  return FROTH_OK;
}

/* Handle a bare identifier at top level: resolve/create slot, then invoke.
 * TODO: Currently pushes a FROTH_CALL cell — needs call/execution machinery. */
static froth_error_t froth_evaluator_handle_identifier(froth_token_t token, froth_stack_t* ds) {
  froth_cell_u_t slot_index;
  froth_error_t err;
  froth_cell_t cell;

  err = resolve_or_create_slot(token.name, &froth_heap, &slot_index);
  if (err != FROTH_OK) { return err; }

  err = froth_make_cell(slot_index, FROTH_CALL, &cell);
  if (err != FROTH_OK) { return err; }

  err = froth_stack_push(ds, cell);
  if (err != FROTH_OK) { return err; }

  return FROTH_OK;
}

/* Build a quotation from the token stream. Called after "[" has been consumed.
 * Reads tokens until the matching "]", writing each into the heap as a
 * quotation body. Returns the tagged QuoteRef through output_cell.
 *
 * Heap layout: [length] [body_cell_0] [body_cell_1] ... [body_cell_n-1]
 * The length cell is patched after all body cells are written (see ADR-008). */
static froth_error_t froth_evaluator_handle_open_bracket(froth_reader_t* reader, froth_stack_t* ds, froth_heap_t* heap, froth_cell_t* output_cell) {
  froth_error_t err;
  froth_token_t token;

  // Reserve the length cell — we'll patch it once we know the body size
  froth_cell_u_t quote_start_offset;
  froth_cell_t* length_cell;
  froth_cell_u_t quote_length = 0;
  froth_heap_allocate_cells(1, heap, &length_cell, &quote_start_offset);

  while (froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF && token.type != FROTH_TOKEN_CLOSE_BRACKET) {
    froth_cell_t* body_cell;

    switch (token.type) {
      case FROTH_TOKEN_NUMBER:
        err = froth_heap_allocate_cells(1, heap, &body_cell, NULL);
        if (err != FROTH_OK) { return err; }

        err = froth_make_cell(token.number, FROTH_NUMBER, body_cell);
        if (err != FROTH_OK) { return err; }

        quote_length++;
        break;

      case FROTH_TOKEN_IDENTIFIER: {
        err = froth_heap_allocate_cells(1, heap, &body_cell, NULL);
        if (err != FROTH_OK) { return err; }

        froth_cell_u_t slot_index;
        err = resolve_or_create_slot(token.name, heap, &slot_index);
        if (err != FROTH_OK) { return err; }

        err = froth_make_cell(slot_index, FROTH_CALL, body_cell);
        if (err != FROTH_OK) { return err; }

        quote_length++;
        break;
      }

      case FROTH_TOKEN_OPEN_BRACKET: {
        // Recurse: build the nested quotation, then store its QuoteRef in our body
        froth_cell_t nested_quote;
        err = froth_evaluator_handle_open_bracket(reader, ds, heap, &nested_quote);
        if (err != FROTH_OK) { return err; }

        err = froth_heap_allocate_cells(1, heap, &body_cell, NULL);
        if (err != FROTH_OK) { return err; }

        *body_cell = nested_quote; // Already tagged as FROTH_QUOTE

        quote_length++;
        break;
      }

      default:
        break;
    }
  }

  if (token.type == FROTH_TOKEN_CLOSE_BRACKET) {
    *length_cell = quote_length;

    err = froth_make_cell(quote_start_offset, FROTH_QUOTE, output_cell);
    if (err != FROTH_OK) { return err; }

    return FROTH_OK;
  }

  return FROTH_ERROR_UNTERMINATED_QUOTATION;
}

/* Top-level evaluator. Reads tokens from input and dispatches each one. */
froth_error_t froth_evaluate_input(char* input, froth_stack_t* ds, froth_heap_t* heap) {
  froth_reader_t reader;
  froth_token_t token;
  froth_error_t err;

  froth_reader_init(&reader, input);

  while (froth_reader_next_token(&reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF) {
    switch (token.type) {
      case FROTH_TOKEN_NUMBER:
        err = froth_evaluator_handle_number(token, ds);
        if (err != FROTH_OK) { return err; }
        break;
      case FROTH_TOKEN_IDENTIFIER:
        err = froth_evaluator_handle_identifier(token, ds);
        if (err != FROTH_OK) { return err; }
        break;
      case FROTH_TOKEN_OPEN_BRACKET: {
        froth_cell_t quote_cell;
        err = froth_evaluator_handle_open_bracket(&reader, ds, heap, &quote_cell);
        if (err != FROTH_OK) { return err; }

        err = froth_stack_push(ds, quote_cell);
        if (err != FROTH_OK) { return err; }
        break;
      }
      default:
        break;
    }
  }

  return FROTH_OK;
}
