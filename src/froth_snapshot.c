#include "froth_snapshot.h"
#include "froth_slot_table.h"
#include "froth_types.h"

froth_error_t froth_snapshot_save(froth_vm_t *froth_vm) {
  const uint8_t snapshot_buffer[FROTH_SNAPSHOT_MAX_BYTES] = {0};

  froth_cell_u_t num_slots = froth_slot_count();
  for (int i = 0; i < num_slots; i++) {
    const char *name;
    froth_cell_t impl;
    if (froth_slot_is_overlay(i)) {
      FROTH_TRY(froth_slot_get_name(i, &name));
      FROTH_TRY(froth_slot_get_impl(i, &impl));
    }
  }
  return FROTH_OK;
}

froth_error_t froth_snapshot_load(froth_vm_t *froth_vm) { return FROTH_OK; }
