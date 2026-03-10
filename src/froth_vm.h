#pragma once

#include "froth_stack.h"
#include "froth_heap.h"
#include <stdbool.h>

struct froth_vm_t {
  froth_stack_t ds;
  froth_stack_t rs;
  froth_stack_t cs;
  froth_heap_t heap;
  froth_cell_t thrown;
  froth_cell_t last_error_slot; /* slot index at point of error, or -1 */
  volatile int interrupted;
  uint8_t boot_complete;
  froth_cell_u_t watermark_heap_offset;
};

extern froth_vm_t froth_vm;
