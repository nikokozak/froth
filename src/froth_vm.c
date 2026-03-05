#include "froth_vm.h"

static froth_cell_t ds_memory[FROTH_DS_CAPACITY];
static froth_cell_t rs_memory[FROTH_RS_CAPACITY];
static froth_cell_t cs_memory[FROTH_CS_CAPACITY];
static uint8_t heap_memory[FROTH_HEAP_SIZE];

froth_vm_t froth_vm = {
  .ds = { .pointer = 0, .capacity = FROTH_DS_CAPACITY, .data = ds_memory },
  .rs = { .pointer = 0, .capacity = FROTH_RS_CAPACITY, .data = rs_memory },
  .cs = { .pointer = 0, .capacity = FROTH_CS_CAPACITY, .data = cs_memory },
  .heap = { .data = heap_memory, .pointer = 0 },
  .thrown = FROTH_OK,
};
