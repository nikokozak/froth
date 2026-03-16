#pragma once
#include "froth_types.h"

#ifndef FROTH_LINE_BUFFER_SIZE
#error "FROTH_LINE_BUFFER_SIZE is not defined."
#endif

froth_error_t froth_repl_init(froth_vm_t *vm);
froth_error_t froth_repl_evaluate(froth_vm_t *vm);
froth_error_t froth_repl_accept_byte(froth_vm_t *vm, char byte, int8_t *state);
