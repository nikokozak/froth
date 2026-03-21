#pragma once

#include "froth_types.h"
#include "froth_vm.h"

froth_error_t froth_execute_quote(froth_vm_t* vm, froth_cell_t quote_cell);
froth_error_t froth_execute_slot(froth_vm_t* vm, froth_cell_u_t slot_index);
