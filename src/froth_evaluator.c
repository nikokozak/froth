#include "froth_types.h"
#include "froth_reader.h"
#include "froth_stack.h"
#include "froth_slot_table.h"

froth_error_t froth_evaluator_handle_number(froth_token_t number, froth_stack_t* froth_ds_stack) {
  froth_cell_t cell;
  froth_error_t err;

  err = froth_make_cell(number.number, FROTH_NUMBER, &cell);
  if (err != FROTH_OK) { return err; }

  err = froth_stack_push(froth_ds_stack, cell);
  if (err != FROTH_OK) { return err; }
  return FROTH_OK;
}

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
    }
  }

  return FROTH_OK;
};
