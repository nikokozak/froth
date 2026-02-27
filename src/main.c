#include "froth_types.h"
#include "froth_stack.h"
#include <stdio.h>

int main() {
  
  froth_stack_push(&froth_ds_stack, 42);
  froth_stack_push(&froth_ds_stack, 69);
  froth_stack_push(&froth_ds_stack, 1337);

  froth_cell_t value;

  froth_stack_pop(&froth_ds_stack, &value);
  fprintf(stdout, "Popped value: %" FROTH_CELL_FORMAT "\n", value);
  froth_stack_pop(&froth_ds_stack, &value);
  fprintf(stdout, "Popped value: %" FROTH_CELL_FORMAT "\n", value);
  froth_stack_peek(&froth_ds_stack, &value);
  fprintf(stdout, "Peeked value: %" FROTH_CELL_FORMAT "\n", value);
  froth_stack_pop(&froth_ds_stack, &value);
  fprintf(stdout, "Popped value: %" FROTH_CELL_FORMAT "\n", value);
  if (froth_stack_pop(&froth_ds_stack, &value) == FROTH_ERROR_STACK_UNDERFLOW) {
    fprintf(stderr, "Stack underflow error!\n");
  }
  if (froth_stack_peek(&froth_ds_stack, &value) == FROTH_ERROR_STACK_UNDERFLOW) {
    fprintf(stderr, "Stack underflow error!\n");
  }

  while (froth_stack_push(&froth_ds_stack, 0) != FROTH_ERROR_STACK_OVERFLOW) { }
  fprintf(stderr, "Stack overflow error!\n");


  return 0;
}
