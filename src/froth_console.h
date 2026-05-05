#pragma once

#include "froth_types.h"
#include "froth_vm.h"
#include "platform.h"
#include <stdbool.h>
#include <stdint.h>

froth_error_t froth_console_emit(uint8_t byte);
froth_error_t froth_console_emit_string(const char *str);
froth_error_t froth_console_flush_output(void);
froth_error_t froth_console_key(froth_vm_t *vm, uint8_t *byte);
bool froth_console_key_ready(void);
void froth_console_poll(froth_vm_t *vm);
bool froth_console_live_active(void);
const char *froth_console_format_number(froth_cell_t number);
