#pragma once

#include "froth_types.h"
#include "froth_vm.h"
#include "platform.h"
#include <stdint.h>

/* Attach recognizer limits. Stray 0x00 eats at most this many bytes / ms. */
#define FROTH_CONSOLE_RECOGNIZE_CAP 64u
#define FROTH_CONSOLE_RECOGNIZE_TIMEOUT_MS 50u

/* Live output buffer: fits one OUTPUT_DATA payload (minus 2-byte length prefix). */
#ifndef FROTH_CONSOLE_OUTPUT_CAP
#define FROTH_CONSOLE_OUTPUT_CAP 128u
#endif

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
  uint16_t seq;         /* next expected request seq */
  uint16_t active_seq;  /* seq of the in-flight eval (for OUTPUT_DATA) */
  uint32_t lease_deadline_ms;
  uint8_t poll_in_frame;

  /* Live output buffer. Flushed on \n, full, or before terminal frames. */
  uint8_t output_buf[FROTH_CONSOLE_OUTPUT_CAP];
  uint16_t output_pos;
} froth_console_t;

/* Main loop. Boots into Direct, never returns. */
froth_error_t froth_console_start(froth_vm_t *vm);

/* Output shim. In Direct mode, forwards to platform_emit.
 * In Live mode, buffers and flushes as OUTPUT_DATA. */
froth_error_t froth_console_emit(uint8_t byte);

/* Flush any buffered Live output as an OUTPUT_DATA frame. No-op in Direct. */
froth_error_t froth_console_flush_output(void);

/* Executor safe-point poll hook. Non-blocking. */
void froth_console_poll(froth_vm_t *vm);
