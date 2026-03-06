#pragma once

#include "froth_types.h"
#include "froth_ffi.h"

#ifndef FROTH_MAX_PERM_SIZE
  #define FROTH_MAX_PERM_SIZE 8
#endif

extern const froth_ffi_entry_t froth_primitives[];

froth_error_t froth_prim_dots(froth_vm_t *froth_vm);
froth_error_t froth_prim_def(froth_vm_t *froth_vm);
