#pragma once

#include "froth_types.h"
#include "froth_vm.h"
#include "platform.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef FROTH_HAS_LIVE

/* Attach recognizer limits. Stray 0x00 eats at most this many bytes / ms. */
#define FROTH_CONSOLE_RECOGNIZE_CAP 64u
#define FROTH_CONSOLE_RECOGNIZE_TIMEOUT_MS 50u

/* Live output buffer: fits one OUTPUT_DATA payload (minus 2-byte length prefix). */
#ifndef FROTH_CONSOLE_OUTPUT_CAP
#define FROTH_CONSOLE_OUTPUT_CAP 128u
#endif

#ifndef FROTH_CONSOLE_INPUT_CAP
#define FROTH_CONSOLE_INPUT_CAP 64u
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

  /* Live input FIFO. Fed by INPUT_DATA, consumed by key/key?. */
  uint8_t input_buf[FROTH_CONSOLE_INPUT_CAP];
  uint8_t input_head;
  uint8_t input_count;
  uint8_t input_wait_sent;
} froth_console_t;

/* Main loop. Boots into Direct, never returns. */
froth_error_t froth_console_start(froth_vm_t *vm);

froth_error_t froth_console_emit(uint8_t byte);
froth_error_t froth_console_flush_output(void);
froth_error_t froth_console_key(froth_vm_t *vm, uint8_t *byte);
bool froth_console_key_ready(void);
void froth_console_poll(froth_vm_t *vm);

#else /* !FROTH_HAS_LIVE — Direct-only passthroughs */

static inline froth_error_t froth_console_emit(uint8_t byte) {
  return platform_emit(byte);
}
static inline froth_error_t froth_console_flush_output(void) {
  return FROTH_OK;
}
static inline froth_error_t froth_console_key(froth_vm_t *vm, uint8_t *byte) {
  (void)vm;
  return platform_key(byte);
}
static inline bool froth_console_key_ready(void) {
  return platform_key_ready();
}
static inline void froth_console_poll(froth_vm_t *vm) {
  platform_check_interrupt(vm);
}

#endif /* FROTH_HAS_LIVE */
