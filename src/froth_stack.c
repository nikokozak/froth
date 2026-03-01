#include "froth_stack.h"

static froth_cell_t ds_memory[FROTH_DS_CAPACITY];
static froth_cell_t rs_memory[FROTH_RS_CAPACITY];
static froth_cell_t cs_memory[FROTH_CS_CAPACITY];

froth_stack_t froth_ds_stack = {
  .pointer = 0, 
  .capacity = FROTH_DS_CAPACITY,
  .data = ds_memory
};

froth_stack_t froth_rs_stack = {
  .pointer = 0, 
  .capacity = FROTH_RS_CAPACITY,
  .data = rs_memory
};

froth_stack_t froth_cs_stack = {
  .pointer = 0, 
  .capacity = FROTH_CS_CAPACITY,
  .data = cs_memory
};

froth_error_t froth_stack_push(froth_stack_t* stack, froth_cell_t value) {
  if (stack->pointer >= stack->capacity) { return FROTH_ERROR_STACK_OVERFLOW; }
  stack->data[stack->pointer++] = value;
  return FROTH_OK;
}
froth_error_t froth_stack_pop(froth_stack_t* stack, froth_cell_t* return_value) {
  if (stack->pointer == 0) { return FROTH_ERROR_STACK_UNDERFLOW; } 
  *return_value = stack->data[--stack->pointer];
  return FROTH_OK;
}
froth_error_t froth_stack_peek(froth_stack_t* stack, froth_cell_t* return_value) {
  if (stack->pointer == 0) { return FROTH_ERROR_STACK_UNDERFLOW; }
  *return_value = stack->data[stack->pointer - 1];
  return FROTH_OK;
}

froth_cell_u_t froth_stack_depth(froth_stack_t* stack) {
  return stack->pointer;
}
