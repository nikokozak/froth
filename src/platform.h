#pragma once

#include "froth_types.h"
#include <stdbool.h>

froth_error_t platform_init(void);
froth_error_t platform_emit(uint8_t byte);
froth_error_t platform_key(uint8_t *byte);
bool platform_key_ready(void);

#ifdef FROTH_HAS_SNAPSHOTS
froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len);
froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len);
froth_error_t platform_snapshot_erase(uint8_t slot);
#endif
