#include "froth_types.h"
#include "froth_reader.h"
#include "froth_stack.h"
#include "froth_slot_table.h"
#include <stdio.h>

froth_error_t froth_evaluator_handle_number(froth_token_t number, froth_stack_t* froth_ds_stack) {
  froth_cell_t cell;
  froth_error_t err;

  err = froth_make_cell(number.number, FROTH_NUMBER, &cell);
  if (err != FROTH_OK) { return err; }

  err = froth_stack_push(froth_ds_stack, cell);
  if (err != FROTH_OK) { return err; }
  return FROTH_OK;
}

/*
  * Each handler will most likely push to the DS after resolving values/impl/etc.
  */

froth_error_t froth_evaluator_handle_identifier(froth_token_t identifier, froth_stack_t* froth_ds_stack) {
  // TODO: Implement calling.
  froth_cell_u_t slot_index;
  froth_error_t err;
  froth_cell_t cell;

  err = froth_slot_find_name(identifier.name, &slot_index);
  if (err == FROTH_ERROR_SLOT_NAME_NOT_FOUND) {
    err = froth_slot_create(identifier.name, &froth_heap, &slot_index);
    if (err != FROTH_OK) { return err; }
  }

  err = froth_make_cell(slot_index, FROTH_CALL, &cell);
  if (err != FROTH_OK) { return err; }

  err = froth_stack_push(froth_ds_stack, cell);
  if (err != FROTH_OK) { return err; }

  return FROTH_OK;

}

froth_error_t froth_evaluator_handle_open_bracket(froth_reader_t* reader, froth_stack_t* froth_ds_stack, froth_heap_t* froth_heap, froth_cell_t* output_cell) {
  froth_error_t err;
  froth_token_t token; // Evaluator gets started with "[", which we don't really care about, so we don't pass this in as a param.

  froth_cell_u_t quote_start_offset;
  froth_cell_t* quote_length_cell; // We will come back and write the length of the quote here once we know it.
  froth_cell_u_t quote_length = 0;
  froth_cell_t cell;
  froth_heap_allocate_cells(1, froth_heap, &quote_length_cell, &quote_start_offset);

  while (froth_reader_next_token(reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF && token.type != FROTH_TOKEN_CLOSE_BRACKET) {
    froth_cell_t* heap_cell; // Aligned cell pointer into heap

    switch (token.type) {
      case FROTH_TOKEN_NUMBER:
        err = froth_heap_allocate_cells(1, froth_heap, &heap_cell, NULL);
        if (err != FROTH_OK) { return err; }

        err = froth_make_cell(token.number, FROTH_NUMBER, heap_cell);
        if (err != FROTH_OK) { return err; }

        quote_length++;
        break;

      case FROTH_TOKEN_IDENTIFIER: {
        err = froth_heap_allocate_cells(1, froth_heap, &heap_cell, NULL);
        if (err != FROTH_OK) { return err; }

        froth_cell_u_t slot_index;
        err = froth_slot_find_name(token.name, &slot_index);

        // Handle creation if we don't have the name in a slot already.
        if (err == FROTH_ERROR_SLOT_NAME_NOT_FOUND) {
          err = froth_slot_create(token.name, froth_heap, &slot_index);
          if (err != FROTH_OK) { return err; }
        } else if (err != FROTH_OK) {
          return err;
        }

        err = froth_make_cell(slot_index, FROTH_CALL, heap_cell);
        if (err != FROTH_OK) { return err; }

        quote_length++;
        break;
      }

      case FROTH_TOKEN_OPEN_BRACKET:
        // Recursively handle nested quotations.
        err = froth_evaluator_handle_open_bracket(reader, froth_ds_stack, froth_heap, &cell);
        if (err != FROTH_OK) { return err; }

        err = froth_heap_allocate_cells(1, froth_heap, &heap_cell, NULL);
        if (err != FROTH_OK) { return err; }

        *heap_cell = cell;

        quote_length++;
        break;
      
    } // end Token switch

  } // end Token while loop

  if (token.type == FROTH_TOKEN_CLOSE_BRACKET) {
    *quote_length_cell = quote_length;

    err = froth_make_cell(quote_start_offset, FROTH_QUOTE, output_cell);
    if (err != FROTH_OK) { return err; }

    return FROTH_OK;
  }

  return FROTH_ERROR_UNTERMINATED_QUOTATION;

}

/*
  * Handle dispatcher for all of the different token types. 
  */

froth_error_t froth_evaluate_input(char* input, froth_stack_t* froth_ds_stack, froth_heap_t* froth_heap) {
 
  froth_reader_t reader;
  froth_token_t token;
  froth_error_t err;

  froth_reader_init(&reader, input);

  while (froth_reader_next_token(&reader, &token) == FROTH_OK && token.type != FROTH_TOKEN_EOF) {
    switch (token.type) {
      case FROTH_TOKEN_NUMBER:
        err = froth_evaluator_handle_number(token, froth_ds_stack);
        if (err != FROTH_OK) { return err; }
        break;
      case FROTH_TOKEN_IDENTIFIER:
        err = froth_evaluator_handle_identifier(token, froth_ds_stack);
        if (err != FROTH_OK) { return err; }
        break;
      case FROTH_TOKEN_OPEN_BRACKET: {
        // We handle pushing at top level here.\e
        // TODO: should all cases explicitely handle the push at top level?
        froth_cell_t quote_cell;
        err = froth_evaluator_handle_open_bracket(&reader, froth_ds_stack, froth_heap, &quote_cell);
        if (err != FROTH_OK) { return err; }

        err = froth_stack_push(froth_ds_stack, quote_cell);
        if (err != FROTH_OK) { return err; }
        break;
      }
    }
  }

  return FROTH_OK;
};
