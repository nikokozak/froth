#pragma once

#include "froth_ffi.h"

/* Boot the Froth VM: register primitives, load stdlib, restore snapshot,
 * run autorun, start REPL. This is a convenience wrapper around public
 * functions — copy and modify if you need custom boot ordering. */
void froth_boot(const froth_ffi_entry_t *board_bindings);
