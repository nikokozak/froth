#include "froth_console.h"
#include "froth_fmt.h"
#include "froth_link.h"
#include "froth_repl.h"
#include "froth_transport.h"
#include "platform.h"
#include <stdbool.h>
#include <string.h>

static froth_console_t g_console;
static const char *prompt_normal = "froth> ";

#define FROTH_CONSOLE_LIVE_LEASE_MS 5000u

/* ATTACH_RES status bytes. */
#define FROTH_ATTACH_STATUS_OK 0u
#define FROTH_ATTACH_STATUS_BUSY 1u
#define FROTH_ATTACH_STATUS_UNSUPPORTED 2u
#define FROTH_ATTACH_STATUS_INVALID 3u

static froth_error_t send_attach_res(uint64_t session_id, uint16_t seq,
                                     uint8_t status) {
  uint8_t payload[1];
  payload[0] = status;
  return froth_link_send_frame(session_id, FROTH_LINK_ATTACH_RES, seq, payload,
                               sizeof(payload));
}

/* Clear recognizer state. Safe to call anytime. */
static void recognize_reset(froth_console_t *console) {
  memset(console->recognize_buf, 0, sizeof(console->recognize_buf));
  console->recognize_pos = 0;
  console->recognize_active = 0;
  console->recognize_start_ms = 0;
}

/* Feed one byte to the recognizer.
 * Returns 1 if a complete candidate is ready, 0 otherwise. */
static int recognize_feed(froth_console_t *console, uint8_t byte) {
  if (!console->recognize_active) {
    if (byte != 0x00)
      return 0;
    recognize_reset(console);
    console->recognize_active = 1;
    console->recognize_start_ms = platform_uptime_ms();
    return 0;
  }

  /* Closing delimiter. Empty candidate is junk. */
  if (byte == 0x00) {
    if (console->recognize_pos == 0) {
      recognize_reset(console);
      return 0;
    }
    return 1;
  }

  /* Overflow: give up. */
  if (console->recognize_pos >= FROTH_CONSOLE_RECOGNIZE_CAP) {
    recognize_reset(console);
    return 0;
  }

  console->recognize_buf[console->recognize_pos++] = byte;
  return 0;
}

/* Returns 1 if the recognizer timed out (and was reset). */
static int recognize_check_timeout(froth_console_t *console) {
  if (!console->recognize_active)
    return 0;
  uint32_t elapsed = platform_uptime_ms() - console->recognize_start_ms;
  if (elapsed >= FROTH_CONSOLE_RECOGNIZE_TIMEOUT_MS) {
    recognize_reset(console);
    return 1;
  }
  return 0;
}

/* Decode + parse a completed candidate. Act on HELLO/ATTACH, discard rest. */
static froth_error_t handle_recognized_frame(froth_vm_t *vm,
                                             froth_console_t *console) {
  froth_error_t err;
  uint16_t decoded_len = 0;
  froth_link_header_t header;
  const uint8_t *payload = NULL;
  uint8_t decoded[FROTH_CONSOLE_RECOGNIZE_CAP];

  if (!console->recognize_active || console->recognize_pos == 0) {
    recognize_reset(console);
    return FROTH_OK;
  }

  err = froth_cobs_decode(console->recognize_buf, console->recognize_pos,
                          decoded, sizeof(decoded), &decoded_len);
  if (err != FROTH_OK) {
    recognize_reset(console);
    return FROTH_OK;
  }

  err = froth_link_header_parse(decoded, decoded_len, &header, &payload);
  if (err != FROTH_OK) {
    recognize_reset(console);
    return FROTH_OK;
  }

  switch (header.message_type) {
  case FROTH_LINK_HELLO_REQ:
    if (header.session_id != 0 || header.seq != 0 || header.payload_length != 0)
      break;
    err = froth_link_send_hello_res(vm, 0, 0);
    recognize_reset(console);
    return err;

  case FROTH_LINK_ATTACH_REQ:
    /* Bad fields -> INVALID. */
    if (header.session_id == 0 || header.seq != 0 ||
        header.payload_length != 0) {
      err = send_attach_res(header.session_id, 0, FROTH_ATTACH_STATUS_INVALID);
      recognize_reset(console);
      return err;
    }
    /* Not at idle prompt -> BUSY. */
    if (console->mode != FROTH_CONSOLE_DIRECT || console->session_id != 0 ||
        !froth_repl_is_idle()) {
      err = send_attach_res(header.session_id, 0, FROTH_ATTACH_STATUS_BUSY);
      recognize_reset(console);
      return err;
    }
    /* Send OK first. If that fails, stay Direct. */
    err = send_attach_res(header.session_id, 0, FROTH_ATTACH_STATUS_OK);
    if (err != FROTH_OK) {
      recognize_reset(console);
      return err;
    }
    console->mode = FROTH_CONSOLE_LIVE;
    console->session_id = header.session_id;
    console->seq = 1;
    console->lease_deadline_ms =
        platform_uptime_ms() + FROTH_CONSOLE_LIVE_LEASE_MS;
    break;

  default:
    break;
  }

  recognize_reset(console);
  return FROTH_OK;
}

/* ── Output shim ───────────────────────────────────────────────────*/

froth_error_t froth_console_flush_output(void) {
  if (g_console.mode != FROTH_CONSOLE_LIVE || g_console.output_pos == 0)
    return FROTH_OK;

  /* OUTPUT_DATA payload: u16 byte_count + raw bytes. */
  uint8_t payload[2 + FROTH_CONSOLE_OUTPUT_CAP];
  uint16_t n = g_console.output_pos;
  payload[0] = n & 0xFF;
  payload[1] = (n >> 8) & 0xFF;
  memcpy(payload + 2, g_console.output_buf, n);

  g_console.output_pos = 0;
  return froth_link_send_frame(g_console.session_id, FROTH_LINK_OUTPUT_DATA,
                               g_console.active_seq, payload, 2 + n);
}

froth_error_t froth_console_emit(uint8_t byte) {
  if (g_console.mode != FROTH_CONSOLE_LIVE)
    return platform_emit(byte);

  g_console.output_buf[g_console.output_pos++] = byte;

  if (byte == '\n' || g_console.output_pos >= FROTH_CONSOLE_OUTPUT_CAP)
    return froth_console_flush_output();

  return FROTH_OK;
}

/* ── Main loop ──────────────────────────────────────────────────────
 * Direct mode: 0x00 -> recognizer, 0x03 -> interrupt, CR/LF ->
 * REPL, everything else -> REPL. Timeout checked each iteration.
 * Live mode: TODO (frame-only dispatch).                            */

froth_error_t froth_console_start(froth_vm_t *vm) {
  uint8_t byte = 0;
  int8_t reader_state = 0;
  int last_was_cr = 0;
  int frame_ready = 0;
  int in_frame = 0;
  froth_error_t err;

  FROTH_TRY(froth_repl_init(vm));

  g_console.mode = FROTH_CONSOLE_DIRECT;
  g_console.session_id = 0;
  g_console.seq = 0;
  g_console.lease_deadline_ms = 0;
  recognize_reset(&g_console);

  FROTH_TRY(emit_string(prompt_normal));

  while (1) {
    if (g_console.recognize_active)
      recognize_check_timeout(&g_console);

    err = platform_key(&byte);
    if (err != FROTH_OK)
      continue;

    /* Live mode: frame-only, no raw bytes. */
    if (g_console.mode == FROTH_CONSOLE_LIVE) {
      /* Lease expired: host is gone, return to Direct. */
      if ((platform_uptime_ms() - g_console.lease_deadline_ms) < 0x80000000u &&
          g_console.lease_deadline_ms != 0) {
        g_console.mode = FROTH_CONSOLE_DIRECT;
        g_console.session_id = 0;
        g_console.seq = 0;
        g_console.lease_deadline_ms = 0;
        in_frame = 0;
        froth_link_frame_reset();
        emit_string(prompt_normal);
        continue;
      }
      if (byte == 0x00 && !in_frame) {
        froth_link_frame_reset();
        in_frame = 1;
        continue;
      }
      if (byte == 0x00 && in_frame) {
        froth_link_header_t header;
        const uint8_t *payload;
        err = froth_link_frame_decode(&header, &payload);
        in_frame = 0;
        if (err != FROTH_OK) {
          froth_link_frame_reset();
          continue; // Drop it, should not happen.
        }
        if (header.session_id != g_console.session_id) {
          froth_link_frame_reset();
          continue; // Drop it, should not happen.
        }
        // Refresh lease on valid frame
        g_console.lease_deadline_ms =
            platform_uptime_ms() + FROTH_CONSOLE_LIVE_LEASE_MS;

        switch (header.message_type) {
        case FROTH_LINK_KEEPALIVE:
          break; // No response, lease already refreshed.
        case FROTH_LINK_DETACH_REQ:
          froth_link_send_frame(header.session_id, FROTH_LINK_DETACH_RES,
                                header.seq, NULL, 0);
          froth_link_frame_reset();

          g_console.session_id = 0;
          g_console.seq = 0;
          g_console.mode = FROTH_CONSOLE_DIRECT;
          g_console.lease_deadline_ms = 0;
          in_frame = 0;

          FROTH_TRY(emit_string(prompt_normal));
          break;
        default:
          froth_link_dispatch(vm, &header, payload);
          break;
        }
        froth_link_frame_reset();
        continue;
      }

      if (byte != 0x00 && in_frame) {
        froth_link_frame_byte(byte);
        continue;
      }
      if (byte != 0x00 && !in_frame) {
        continue; // Drop it, should not happen.
      }
    }

    /* Recognizer eats 0x00 or any bytes while accumulating. */
    if (byte == 0x00 || g_console.recognize_active) {
      frame_ready = recognize_feed(&g_console, byte);
      if (frame_ready)
        FROTH_TRY(handle_recognized_frame(vm, &g_console));
      continue;
    }

    if (byte == 0x03) {
      vm->interrupted = 1;
      continue;
    }

    /* CRLF coalescing. */
    if (byte == '\n' && last_was_cr) {
      last_was_cr = 0;
      continue;
    }
    last_was_cr = (byte == '\r');
    if (byte == '\r')
      byte = '\n';

    reader_state = 0;
    FROTH_TRY(froth_repl_accept_byte(vm, (char)byte, &reader_state));
    if (reader_state == 1) {
      FROTH_TRY(froth_repl_evaluate(vm));
      FROTH_TRY(emit_string(prompt_normal));
    }
  }
}
