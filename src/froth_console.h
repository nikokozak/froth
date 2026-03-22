#pragma once

#include "froth_types.h"
#include "froth_vm.h"
#include <stdint.h>

/* Attach recognizer limits. Stray 0x00 eats at most this many bytes / ms. */
#define FROTH_CONSOLE_RECOGNIZE_CAP 64u
#define FROTH_CONSOLE_RECOGNIZE_TIMEOUT_MS 50u

typedef enum {
  FROTH_CONSOLE_DIRECT = 0,
  FROTH_CONSOLE_LIVE = 1,
} froth_console_mode_t;

typedef struct {
  froth_console_mode_t mode;

  /* Bounded recognizer for HELLO/ATTACH in Direct mode. */
  uint8_t recognize_buf[FROTH_CONSOLE_RECOGNIZE_CAP];
  uint8_t recognize_pos;
  uint8_t recognize_active;
  uint32_t recognize_start_ms;

  /* Live session state. session_id == 0 means Direct. */
  uint64_t session_id;
  uint16_t seq;
  uint32_t lease_deadline_ms;
} froth_console_t;

/* Main loop. Boots into Direct, never returns. */
froth_error_t froth_console_start(froth_vm_t *vm);
