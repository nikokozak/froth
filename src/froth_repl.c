#include "froth_repl.h"
#include <stdio.h>
#include <stdbool.h>

char repl_buffer[FROTH_LINE_BUFFER_SIZE];
const char* repl_prompt = "froth> ";

static char* froth_printify_number(froth_cell_t number) {
  static char num_buffer[32]; // Enough to hold any 64-bit integer
  snprintf(num_buffer, sizeof(num_buffer), "%" FROTH_CELL_FORMAT, number);
  return num_buffer;
}

froth_error_t froth_repl_start(void) {
  froth_error_t err;
  while (true) {
    err = froth_repl_print_promt();
    if (err != FROTH_OK) { return err; }

    err = froth_repl_read_line(repl_buffer);
    if (err != FROTH_OK) { return err; }
   
    err = froth_evaluate_input(repl_buffer, &froth_ds_stack, &froth_heap);

    if (err != FROTH_OK) {
      const char* error_msg = "Error evaluating input\n";
      for (const char* p = error_msg; *p != '\0'; p++) {
        froth_error_t emit_err = platform_emit((uint8_t)*p);
        if (emit_err != FROTH_OK) { return emit_err; }
      }
    } else {
      err = froth_repl_print_stack(&froth_ds_stack, froth_stack_depth(&froth_ds_stack));
      if (err != FROTH_OK) { return err; }
    }
  }
}

froth_error_t froth_repl_read_line(char* output_buffer) {

  froth_cell_u_t pos = 0;
  while (pos < FROTH_LINE_BUFFER_SIZE - 1) {
    uint8_t byte;
    froth_error_t err = platform_key(&byte);
    if (err != FROTH_OK) { return err; }

    if (byte == '\n') {
      break;
    }

    output_buffer[pos++] = byte;
  }
  output_buffer[pos] = '\0';
  return FROTH_OK;
};

froth_error_t froth_repl_print_promt(void) {
  for (const char* p = repl_prompt; *p != '\0'; p++) {
    froth_error_t err = platform_emit((uint8_t)*p);
    if (err != FROTH_OK) { return err; }
  }
  return FROTH_OK;
}

froth_error_t froth_repl_print_stack(froth_stack_t* stack, froth_cell_u_t depth) {
  froth_error_t err;
  froth_cell_t cell;

  for (froth_cell_u_t i = 0; i < depth; i++) {
    cell = stack->data[i];
    if (FROTH_GET_CELL_TAG(cell) == FROTH_NUMBER) {
      char* num_str = froth_printify_number(FROTH_STRIP_CELL_TAG(cell));
      for (char* p = num_str; *p != '\0'; p++) {
        err = platform_emit((uint8_t)*p);
        if (err != FROTH_OK) { return err; }
      }
      err = platform_emit((uint8_t)' '); // Space between cells
      if (err != FROTH_OK) { return err; }
    } else {
      // For non-number cells, just print a placeholder.
      const char* placeholder = "<cell>";
      for (const char* p = placeholder; *p != '\0'; p++) {
        err = platform_emit((uint8_t)*p);
        if (err != FROTH_OK) { return err; }
      }
      err = platform_emit((uint8_t)' '); // Space between cells
      if (err != FROTH_OK) { return err; }
    }
  }

  return FROTH_OK;
}

