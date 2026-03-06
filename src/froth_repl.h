#pragma once
#include "froth_types.h"

#ifndef FROTH_LINE_BUFFER_SIZE
  #error "FROTH_LINE_BUFFER_SIZE is not defined."
#endif

froth_error_t froth_repl_start(froth_vm_t* vm);
