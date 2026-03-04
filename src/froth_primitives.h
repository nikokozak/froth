#pragma once
#include "froth_types.h"
#include "froth_slot_table.h"

#ifndef FROTH_MAX_PERM_SIZE
  #define FROTH_MAX_PERM_SIZE 8
#endif

typedef struct {
  const char* name;
  froth_error_t (*prim_word)(froth_vm_t* froth_vm);
} froth_primitive_t;

froth_error_t froth_primitives_register(froth_vm_t* froth_vm);
