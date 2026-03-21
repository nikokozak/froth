#pragma once

#include "froth_types.h"
#include <stdbool.h>

struct froth_vm_t;

froth_error_t platform_init(void);
froth_error_t platform_emit(uint8_t byte);
froth_error_t platform_emit_raw(uint8_t byte); /* no line-ending conversion */
froth_error_t platform_key(uint8_t *byte);
bool platform_key_ready(void);
void platform_check_interrupt(struct froth_vm_t *vm);
void platform_delay_ms(froth_cell_u_t ms);

_Noreturn void platform_fatal(void);

#ifdef FROTH_HAS_SNAPSHOTS
froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len);
froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len);
froth_error_t platform_snapshot_erase(uint8_t slot);
#endif
