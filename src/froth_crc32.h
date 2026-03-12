#pragma once
#include <stddef.h>
#include <stdint.h>

/* IEEE 802.3 CRC32, bitwise (no lookup table). */
uint32_t froth_crc32(const uint8_t *data, size_t len);
