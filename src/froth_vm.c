#include "froth_vm.h"
#include "froth_types.h"

static uint8_t heap_memory[FROTH_HEAP_SIZE];
static froth_cell_t cellspace_memory[FROTH_DATA_SPACE_SIZE];
static froth_cell_t cellspace_base_seed_memory[FROTH_DATA_SPACE_SIZE];

froth_vm_t froth_vm = {
    .heap = {.data = heap_memory, .pointer = 0},
    .cellspace = {.data = cellspace_memory,
                  .base_seed = cellspace_base_seed_memory,
                  .capacity = FROTH_DATA_SPACE_SIZE},
    .thrown = FROTH_OK,
    .last_error_slot = -1,
    .interrupted = 0,
    .boot_complete = 0,
    .watermark_heap_offset = 0,
    .mark_offset = (froth_cell_u_t)-1,
};
