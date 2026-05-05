#include "froth_console.h"

#include <stdio.h>

froth_error_t froth_console_emit(uint8_t byte) { return platform_emit(byte); }

froth_error_t froth_console_emit_string(const char *str) {
  for (const char *p = str; *p != '\0'; p++) {
    FROTH_TRY(froth_console_emit((uint8_t)*p));
  }
  return FROTH_OK;
}

froth_error_t froth_console_flush_output(void) { return FROTH_OK; }

froth_error_t froth_console_key(froth_vm_t *vm, uint8_t *byte) {
  (void)vm;
  return platform_key(byte);
}

bool froth_console_key_ready(void) { return platform_key_ready(); }

void froth_console_poll(froth_vm_t *vm) { platform_check_interrupt(vm); }

bool froth_console_live_active(void) { return false; }

const char *froth_console_format_number(froth_cell_t number) {
  static char buf[32];
  snprintf(buf, sizeof(buf), "%" FROTH_CELL_FORMAT, number);
  return buf;
}
