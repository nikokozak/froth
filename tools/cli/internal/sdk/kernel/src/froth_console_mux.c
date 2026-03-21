#include "froth_console_mux.h"
#include "froth_fmt.h"
#include "froth_repl.h"
#include "froth_vm.h"
#include "platform.h"

#ifdef FROTH_HAS_LINK
#include "froth_transport.h"
#endif

static const char *prompt_normal = "froth> ";

typedef enum { MUX_DIRECT, MUX_FRAME } mux_state_t;

froth_error_t froth_console_mux_start(froth_vm_t *vm) {
  int8_t reader_state;
  mux_state_t mode = MUX_DIRECT;
  int last_was_cr = 0;
  FROTH_TRY(emit_string(prompt_normal));

  while (1) {
    uint8_t byte;
    reader_state = 0;

    froth_error_t err = platform_key(&byte);
    if (err != FROTH_OK)
      continue;

#ifdef FROTH_HAS_LINK
    if (mode == MUX_FRAME) {
      if (byte == 0x00) {
        /* Frame terminated. Decode + dispatch. */
        FROTH_TRY(froth_link_frame_complete(vm));
        mode = MUX_DIRECT;
      } else {
        froth_link_frame_byte(byte);
      }
      continue;
    }

    /* MUX_DIRECT */
    if (byte == 0x00) {
      froth_link_frame_reset();
      mode = MUX_FRAME;
      continue;
    }
#endif

    /* Ctrl-C in direct mode triggers interrupt, not passed to REPL. */
    if (byte == 0x03) {
      vm->interrupted = 1;
      continue;
    }

    /* CRLF coalescing: CR was already converted to LF below. If the
       next byte is LF (trailing half of CRLF), swallow it. */
    if (byte == '\n' && last_was_cr) {
      last_was_cr = 0;
      continue;
    }
    last_was_cr = (byte == '\r');

    /* CR → LF for REPL (VFS conversion is disabled for binary safety). */
    if (byte == '\r')
      byte = '\n';

    /* Direct-mode byte → REPL */
    FROTH_TRY(froth_repl_accept_byte(vm, byte, &reader_state));

    if (reader_state == 1) {
      FROTH_TRY(froth_repl_evaluate(vm));
      FROTH_TRY(emit_string(prompt_normal));
    }
  }
}
