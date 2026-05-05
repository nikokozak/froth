#include "froth_vm.h"
#include "froth_slot_table.h"
#include "froth_types.h"

static uint8_t heap_memory[FROTH_HEAP_SIZE];
static froth_cell_t cellspace_memory[FROTH_DATA_SPACE_SIZE];
static froth_cell_t cellspace_base_seed_memory[FROTH_DATA_SPACE_SIZE];

froth_vm_t froth_vm = {
    .heap = {.data = heap_memory, .pointer = 0},
    .cellspace = {.data = cellspace_memory,
                  .base_seed = cellspace_base_seed_memory,
                  .capacity = FROTH_DATA_SPACE_SIZE},
    .interrupted = 0,
    .boot_complete = 0,
    .watermark_heap_offset = 0,
};

void froth_vm_reset(void) {
  frothy_runtime_free(&froth_vm.frothy_runtime);
  froth_slot_reset_all();
  froth_vm.heap.pointer = 0;
  froth_vm.heap.high_water = 0;
  froth_vm.interrupted = 0;
  froth_vm.boot_complete = 0;
  froth_vm.watermark_heap_offset = 0;
  froth_cellspace_init(&froth_vm.cellspace);
  frothy_runtime_init(&froth_vm.frothy_runtime, &froth_vm.cellspace);
}

void froth_vm_mark_boot_complete(void) {
  froth_vm.boot_complete = 1;
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;
}
