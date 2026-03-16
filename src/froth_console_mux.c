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

    /* Direct-mode byte → REPL */
    FROTH_TRY(froth_repl_accept_byte(vm, byte, &reader_state));

    if (reader_state == 1) {
      FROTH_TRY(froth_repl_evaluate(vm));
      FROTH_TRY(emit_string(prompt_normal));
    }
  }
}
