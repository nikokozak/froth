#pragma once

#include "froth_types.h"
#include <stdbool.h>
#include <stdint.h>

froth_error_t platform_emit(uint8_t byte);
froth_error_t platform_key(uint8_t* byte);
bool platform_key_ready(void);
