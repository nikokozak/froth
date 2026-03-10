#pragma once
#include "froth_types.h"

#define FROTH_SNAPSHOT_MAGIC "FRTHSNAP\0"
#define FROTH_SNAPSHOT_VERSION 1
#define FROTH_SNAPSHOT_MAX_BYTES 1024

typedef struct {
  uint8_t *data;
  froth_cell_u_t position;

}

froth_snapshot_save(froth_vm_t *froth_vm);
froth_error_t froth_snapshot_load(froth_vm_t *froth_vm);
