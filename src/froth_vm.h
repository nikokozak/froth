#pragma once

#include "froth_cellspace.h"
#include "froth_heap.h"
#include "frothy_value.h"
#include <stdbool.h>

struct froth_vm_t {
  froth_heap_t heap;
  froth_cellspace_t cellspace;
  volatile int interrupted;
  uint8_t boot_complete;
  froth_cell_u_t watermark_heap_offset;
  frothy_runtime_t frothy_runtime;
};

extern froth_vm_t froth_vm;

void froth_vm_reset(void);
void froth_vm_mark_boot_complete(void);
