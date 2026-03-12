#include "ffi.h"
#include "froth_fmt.h"
#include <unistd.h>

/* POSIX board package: stub GPIO + real ms delay.
 * gpio.mode and gpio.write print trace output so you can
 * "see" a blink demo in the terminal. */

FROTH_FFI(prim_gpio_mode, "gpio.mode", "( pin mode -- )", "Set pin mode (1=output)") {
  FROTH_POP(mode);
  FROTH_POP(pin);
  emit_string("[gpio] pin ");
  emit_string(format_number(pin));
  emit_string(mode == 1 ? " -> OUTPUT\n" : " -> INPUT\n");
  return FROTH_OK;
}

FROTH_FFI(prim_gpio_write, "gpio.write", "( pin value -- )", "Write digital output") {
  FROTH_POP(value);
  FROTH_POP(pin);
  emit_string("[gpio] pin ");
  emit_string(format_number(pin));
  emit_string(value ? " = HIGH\n" : " = LOW\n");
  return FROTH_OK;
}

FROTH_FFI(prim_ms, "ms", "( n -- )", "Delay n milliseconds") {
  FROTH_POP(ms);
  usleep((useconds_t)ms * 1000);
  return FROTH_OK;
}

FROTH_BOARD_BEGIN(froth_board_bindings)
  FROTH_BIND(prim_gpio_mode),
  FROTH_BIND(prim_gpio_write),
  FROTH_BIND(prim_ms),
FROTH_BOARD_END
