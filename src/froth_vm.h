#pragma once

#include "froth_stack.h"
#include "froth_heap.h"

struct froth_vm_t {
  froth_stack_t ds;
  froth_stack_t rs;
  froth_stack_t cs;
  froth_heap_t heap;
  froth_cell_t thrown;
  froth_cell_t last_error_slot; /* slot index at point of error, or -1 */
};

extern froth_vm_t froth_vm;
