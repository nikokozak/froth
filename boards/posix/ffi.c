#include "froth_vm.h"
#include "ffi.h"
#include "froth_console.h"
#include "frothy_ffi.h"
#include "platform.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* POSIX board package: stub GPIO + real ms delay.
 * gpio.mode and gpio.write print trace output so you can
 * "see" a blink demo in the terminal. */

#define POSIX_I2C_MAX_BUSES 2
#define POSIX_I2C_MAX_DEVICES 8
#define POSIX_UART_MAX_PORTS 2
#define POSIX_PIN_A0 0
#define POSIX_PIN_LED_BUILTIN 2
#define POSIX_PIN_UART_RX 16
#define POSIX_PIN_UART_TX 17
#define POSIX_PIN_SDA 21
#define POSIX_PIN_SCL 22

typedef struct {
  int in_use;
  froth_cell_t sda;
  froth_cell_t scl;
  froth_cell_t freq;
} posix_i2c_bus_t;

typedef struct {
  int in_use;
  froth_cell_t bus;
  froth_cell_t addr;
  froth_cell_t speed;
} posix_i2c_device_t;

typedef struct {
  int in_use;
  froth_cell_t tx;
  froth_cell_t rx;
  froth_cell_t baud;
  uint8_t read_index;
} posix_uart_t;

static posix_i2c_bus_t posix_i2c_buses[POSIX_I2C_MAX_BUSES];
static posix_i2c_device_t posix_i2c_devices[POSIX_I2C_MAX_DEVICES];
static posix_uart_t posix_uarts[POSIX_UART_MAX_PORTS];
static uint8_t posix_gpio_known[40];
static froth_cell_t posix_gpio_levels[40];
static uint32_t posix_random_state = 1;
static const uint8_t posix_uart_readback[] = {'f', 'r', 'o', 't', 'h'};

void froth_board_reset_runtime_state(void) {
  memset(posix_i2c_buses, 0, sizeof(posix_i2c_buses));
  memset(posix_i2c_devices, 0, sizeof(posix_i2c_devices));
  memset(posix_uarts, 0, sizeof(posix_uarts));
  memset(posix_gpio_known, 0, sizeof(posix_gpio_known));
  memset(posix_gpio_levels, 0, sizeof(posix_gpio_levels));
  posix_random_state = frothy_ffi_random_seed(1);
}

static int posix_gpio_pin_valid(froth_cell_t pin) {
  switch (pin) {
  case POSIX_PIN_A0:
  case POSIX_PIN_LED_BUILTIN:
  case POSIX_PIN_UART_RX:
  case POSIX_PIN_UART_TX:
  case POSIX_PIN_SDA:
  case POSIX_PIN_SCL:
    return 1;
  default:
    return 0;
  }
}

static int posix_adc_pin_valid(froth_cell_t pin) { return pin == POSIX_PIN_A0; }

static froth_error_t posix_poll_interruptible_wait(void) {
  platform_check_interrupt(&froth_vm);
  if (!froth_vm.interrupted) {
    return FROTH_OK;
  }

  froth_vm.interrupted = 0;
  return FROTH_ERROR_PROGRAM_INTERRUPTED;
}

static froth_error_t emit_trace_prefix(const char *prefix, froth_cell_t handle) {
  FROTH_TRY(froth_console_emit_string(prefix));
  FROTH_TRY(froth_console_emit_string(froth_console_format_number(handle)));
  FROTH_TRY(froth_console_emit_string("] "));
  return FROTH_OK;
}

#define POSIX_UNUSED_CALLBACK_CONTEXT()                                        \
  do {                                                                         \
    (void)runtime;                                                             \
    (void)context;                                                             \
    (void)arg_count;                                                           \
  } while (0)

static froth_error_t prim_gpio_mode(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;
  int32_t mode = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &mode));

  if (!posix_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  posix_gpio_known[pin] = 1;
  froth_console_emit_string("[gpio] pin ");
  froth_console_emit_string(froth_console_format_number(pin));
  froth_console_emit_string(mode == 1 ? " -> OUTPUT\n" : " -> INPUT\n");
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_gpio_write(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;
  int32_t value = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &value));

  if (!posix_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  posix_gpio_known[pin] = 1;
  posix_gpio_levels[pin] = value ? 1 : 0;
  froth_console_emit_string("[gpio] pin ");
  froth_console_emit_string(froth_console_format_number(pin));
  froth_console_emit_string(value ? " = HIGH\n" : " = LOW\n");
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_gpio_read(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));

  if (!posix_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  if (!posix_gpio_known[pin]) {
    posix_gpio_known[pin] = 1;
    posix_gpio_levels[pin] = 0;
  }

  return frothy_ffi_return_int((int32_t)posix_gpio_levels[pin], out);
}

static froth_error_t prim_ms(frothy_runtime_t *runtime, const void *context,
                             const frothy_value_t *args, size_t arg_count,
                             frothy_value_t *out) {
  int32_t ms = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &ms));

  if (ms <= 0) {
    return frothy_ffi_return_nil(out);
  }
  while (ms > 0) {
    int32_t chunk = ms > 10 ? 10 : ms;

    usleep((useconds_t)chunk * 1000);
    ms -= chunk;
    FROTH_TRY(posix_poll_interruptible_wait());
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_millis(frothy_runtime_t *runtime, const void *context,
                                 const frothy_value_t *args, size_t arg_count,
                                 frothy_value_t *out) {
  POSIX_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  return frothy_ffi_return_int(
      (int32_t)frothy_ffi_wrap_uptime_ms(platform_uptime_ms()), out);
}

static froth_error_t prim_adc_read(frothy_runtime_t *runtime,
                                   const void *context,
                                   const frothy_value_t *args,
                                   size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));

  if (!posix_adc_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_ffi_return_int(2048 + (pin & 0xff), out);
}

static froth_error_t prim_random_seed(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count, frothy_value_t *out) {
  int32_t seed = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &seed));
  posix_random_state = frothy_ffi_random_seed((uint32_t)seed);
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_random_seed_from_millis(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  POSIX_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  posix_random_state = frothy_ffi_random_seed(platform_uptime_ms());
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_random_next(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count, frothy_value_t *out) {
  POSIX_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  return frothy_ffi_return_int(frothy_ffi_random_next_int(&posix_random_state),
                               out);
}

static froth_error_t prim_random_below(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  int32_t limit = 0;
  uint32_t value = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &limit));
  if (limit <= 0) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_TRY(frothy_ffi_random_below(&posix_random_state, (uint32_t)limit,
                                    &value));
  return frothy_ffi_return_int((int32_t)value, out);
}

static froth_error_t prim_random_range(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  int32_t lo = 0;
  int32_t hi = 0;
  uint32_t offset = 0;
  int64_t span = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &lo));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &hi));
  if (lo > hi) {
    int32_t tmp = lo;
    lo = hi;
    hi = tmp;
  }

  span = (int64_t)hi - (int64_t)lo + 1;
  if (span <= 0) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_TRY(
      frothy_ffi_random_below(&posix_random_state, (uint32_t)span, &offset));
  return frothy_ffi_return_int((int32_t)((int64_t)lo + (int64_t)offset), out);
}

static froth_error_t prim_i2c_init(frothy_runtime_t *runtime,
                                   const void *context,
                                   const frothy_value_t *args,
                                   size_t arg_count, frothy_value_t *out) {
  int32_t sda = 0;
  int32_t scl = 0;
  int32_t freq = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &sda));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &scl));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &freq));

  for (int i = 0; i < POSIX_I2C_MAX_BUSES; i++) {
    if (posix_i2c_buses[i].in_use) {
      continue;
    }
    posix_i2c_buses[i] =
        (posix_i2c_bus_t){.in_use = 1, .sda = sda, .scl = scl, .freq = freq};
    return frothy_ffi_return_int(i, out);
  }

  return FROTH_ERROR_BOUNDS;
}

static froth_error_t prim_i2c_add_device(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t bus = 0;
  int32_t addr = 0;
  int32_t speed = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &bus));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &addr));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &speed));

  if (bus < 0 || bus >= POSIX_I2C_MAX_BUSES || !posix_i2c_buses[bus].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  for (int i = 0; i < POSIX_I2C_MAX_DEVICES; i++) {
    if (posix_i2c_devices[i].in_use) {
      continue;
    }
    posix_i2c_devices[i] = (posix_i2c_device_t){
        .in_use = 1, .bus = bus, .addr = addr, .speed = speed};
    return frothy_ffi_return_int(i, out);
  }

  return FROTH_ERROR_BOUNDS;
}

static froth_error_t prim_i2c_rm_device(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t device = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &device));
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  posix_i2c_devices[device] = (posix_i2c_device_t){0};
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_i2c_del_bus(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count, frothy_value_t *out) {
  int32_t bus = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &bus));
  if (bus < 0 || bus >= POSIX_I2C_MAX_BUSES || !posix_i2c_buses[bus].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  posix_i2c_buses[bus] = (posix_i2c_bus_t){0};
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_i2c_probe(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  int32_t bus = 0;
  int32_t addr = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &bus));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &addr));
  if (bus < 0 || bus >= POSIX_I2C_MAX_BUSES || !posix_i2c_buses[bus].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  return frothy_ffi_return_int((addr >= 0 && addr <= 0x7f) ? -1 : 0, out);
}

static froth_error_t prim_i2c_write_byte(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t device = 0;
  int32_t byte = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &device));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &byte));
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  emit_trace_prefix("[i2c", device);
  froth_console_emit_string("write-byte ");
  froth_console_emit_string(froth_console_format_number(byte));
  froth_console_emit_string("\n");
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_i2c_read_byte(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t device = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &device));
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  return frothy_ffi_return_int((int32_t)(posix_i2c_devices[device].addr & 0xff),
                               out);
}

static froth_error_t prim_i2c_write_reg(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t byte = 0;
  int32_t device = 0;
  int32_t reg = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &byte));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &device));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &reg));
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  emit_trace_prefix("[i2c", device);
  froth_console_emit_string("write-reg ");
  froth_console_emit_string(froth_console_format_number(reg));
  froth_console_emit_string(" <- ");
  froth_console_emit_string(froth_console_format_number(byte));
  froth_console_emit_string("\n");
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_i2c_read_reg(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  int32_t device = 0;
  int32_t reg = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &device));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &reg));
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  return frothy_ffi_return_int(
      (int32_t)((posix_i2c_devices[device].addr + reg) & 0xff), out);
}

static froth_error_t prim_i2c_read_reg16(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t device = 0;
  int32_t reg = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &device));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &reg));
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  return frothy_ffi_return_int(
      (int32_t)(((posix_i2c_devices[device].addr & 0xff) << 8) |
                (reg & 0xff)),
      out);
}

static froth_error_t prim_uart_init(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  int32_t tx = 0;
  int32_t rx = 0;
  int32_t baud = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &tx));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &rx));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &baud));

  for (int i = 0; i < POSIX_UART_MAX_PORTS; i++) {
    if (posix_uarts[i].in_use) {
      continue;
    }
    posix_uarts[i] = (posix_uart_t){
        .in_use = 1, .tx = tx, .rx = rx, .baud = baud, .read_index = 0};
    return frothy_ffi_return_int(i, out);
  }

  return FROTH_ERROR_BOUNDS;
}

static froth_error_t prim_uart_write(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t byte = 0;
  int32_t uart = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &byte));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &uart));

  if (uart < 0 || uart >= POSIX_UART_MAX_PORTS || !posix_uarts[uart].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(platform_emit((uint8_t)(byte & 0xff)));
  return frothy_ffi_return_nil(out);
}

static froth_error_t prim_uart_read(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  int32_t uart = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &uart));

  if (uart < 0 || uart >= POSIX_UART_MAX_PORTS || !posix_uarts[uart].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  uint8_t byte =
      posix_uart_readback[posix_uarts[uart].read_index %
                          (uint8_t)sizeof(posix_uart_readback)];
  posix_uarts[uart].read_index++;
  return frothy_ffi_return_int(byte, out);
}

static froth_error_t prim_uart_available(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t uart = 0;

  POSIX_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &uart));

  if (uart < 0 || uart >= POSIX_UART_MAX_PORTS || !posix_uarts[uart].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  return frothy_ffi_return_int(-1, out);
}

static const frothy_ffi_param_t posix_pin_mode_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("mode"),
};

static const frothy_ffi_param_t posix_pin_level_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("level"),
};

static const frothy_ffi_param_t posix_pin_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
};

static const frothy_ffi_param_t posix_delay_params[] = {
    FROTHY_FFI_PARAM_INT("ms"),
};

static const frothy_ffi_param_t posix_random_seed_params[] = {
    FROTHY_FFI_PARAM_INT("seed"),
};

static const frothy_ffi_param_t posix_random_below_params[] = {
    FROTHY_FFI_PARAM_INT("limit"),
};

static const frothy_ffi_param_t posix_random_range_params[] = {
    FROTHY_FFI_PARAM_INT("lo"),
    FROTHY_FFI_PARAM_INT("hi"),
};

static const frothy_ffi_param_t posix_i2c_init_params[] = {
    FROTHY_FFI_PARAM_INT("sda"),
    FROTHY_FFI_PARAM_INT("scl"),
    FROTHY_FFI_PARAM_INT("freq"),
};

static const frothy_ffi_param_t posix_i2c_add_device_params[] = {
    FROTHY_FFI_PARAM_INT("bus"),
    FROTHY_FFI_PARAM_INT("addr"),
    FROTHY_FFI_PARAM_INT("speed"),
};

static const frothy_ffi_param_t posix_i2c_device_params[] = {
    FROTHY_FFI_PARAM_INT("device"),
};

static const frothy_ffi_param_t posix_i2c_bus_params[] = {
    FROTHY_FFI_PARAM_INT("bus"),
};

static const frothy_ffi_param_t posix_i2c_probe_params[] = {
    FROTHY_FFI_PARAM_INT("bus"),
    FROTHY_FFI_PARAM_INT("addr"),
};

static const frothy_ffi_param_t posix_i2c_write_byte_params[] = {
    FROTHY_FFI_PARAM_INT("device"),
    FROTHY_FFI_PARAM_INT("byte"),
};

static const frothy_ffi_param_t posix_i2c_write_reg_params[] = {
    FROTHY_FFI_PARAM_INT("byte"),
    FROTHY_FFI_PARAM_INT("device"),
    FROTHY_FFI_PARAM_INT("reg"),
};

static const frothy_ffi_param_t posix_i2c_read_reg_params[] = {
    FROTHY_FFI_PARAM_INT("device"),
    FROTHY_FFI_PARAM_INT("reg"),
};

static const frothy_ffi_param_t posix_uart_init_params[] = {
    FROTHY_FFI_PARAM_INT("tx"),
    FROTHY_FFI_PARAM_INT("rx"),
    FROTHY_FFI_PARAM_INT("baud"),
};

static const frothy_ffi_param_t posix_uart_write_params[] = {
    FROTHY_FFI_PARAM_INT("byte"),
    FROTHY_FFI_PARAM_INT("uart"),
};

static const frothy_ffi_param_t posix_uart_params[] = {
    FROTHY_FFI_PARAM_INT("uart"),
};

#define POSIX_ENTRY(name_text, params_value, arity_value, result_value,        \
                    help_text, callback_value, effect_text)                   \
  {                                                                            \
      .name = name_text,                                                       \
      .params = params_value,                                                  \
      .param_count = arity_value,                                              \
      .arity = arity_value,                                                    \
      .result_type = result_value,                                             \
      .help = help_text,                                                       \
      .flags = FROTHY_FFI_FLAG_NONE,                                           \
      .callback = callback_value,                                              \
      .context = NULL,                                                         \
      .stack_effect = effect_text,                                             \
  }

const frothy_ffi_entry_t frothy_board_bindings[] = {
    POSIX_ENTRY("gpio.mode", posix_pin_mode_params,
                FROTHY_FFI_PARAM_COUNT(posix_pin_mode_params),
                FROTHY_FFI_VALUE_NIL, "Set pin mode (1=output)",
                prim_gpio_mode, "( pin mode -- )"),
    POSIX_ENTRY("gpio.write", posix_pin_level_params,
                FROTHY_FFI_PARAM_COUNT(posix_pin_level_params),
                FROTHY_FFI_VALUE_NIL, "Write digital output",
                prim_gpio_write, "( pin level -- )"),
    POSIX_ENTRY("gpio.read", posix_pin_params,
                FROTHY_FFI_PARAM_COUNT(posix_pin_params), FROTHY_FFI_VALUE_INT,
                "Read the last written GPIO level on POSIX.", prim_gpio_read,
                "( pin -- value )"),
    POSIX_ENTRY("ms", posix_delay_params,
                FROTHY_FFI_PARAM_COUNT(posix_delay_params),
                FROTHY_FFI_VALUE_NIL, "Delay n milliseconds", prim_ms,
                "( ms -- )"),
    POSIX_ENTRY("millis", NULL, 0, FROTHY_FFI_VALUE_INT,
                "Return wrapped monotonic uptime in milliseconds.",
                prim_millis, "( -- n )"),
    POSIX_ENTRY("adc.read", posix_pin_params,
                FROTHY_FFI_PARAM_COUNT(posix_pin_params), FROTHY_FFI_VALUE_INT,
                "Deterministic ADC stub on POSIX", prim_adc_read,
                "( pin -- value )"),
    POSIX_ENTRY("random.seed!", posix_random_seed_params,
                FROTHY_FFI_PARAM_COUNT(posix_random_seed_params),
                FROTHY_FFI_VALUE_NIL,
                "Seed the board pseudo-random generator.", prim_random_seed,
                "( seed -- )"),
    POSIX_ENTRY("random.seedFromMillis!", NULL, 0, FROTHY_FFI_VALUE_NIL,
                "Seed the board pseudo-random generator from millis.",
                prim_random_seed_from_millis, "( -- )"),
    POSIX_ENTRY("random.next", NULL, 0, FROTHY_FFI_VALUE_INT,
                "Return the next non-negative pseudo-random integer.",
                prim_random_next, "( -- n )"),
    POSIX_ENTRY("random.below", posix_random_below_params,
                FROTHY_FFI_PARAM_COUNT(posix_random_below_params),
                FROTHY_FFI_VALUE_INT,
                "Return a pseudo-random integer in [0, limit).",
                prim_random_below, "( limit -- n )"),
    POSIX_ENTRY("random.range", posix_random_range_params,
                FROTHY_FFI_PARAM_COUNT(posix_random_range_params),
                FROTHY_FFI_VALUE_INT,
                "Return a pseudo-random integer between lo and hi inclusive.",
                prim_random_range, "( lo hi -- n )"),
    POSIX_ENTRY("i2c.init", posix_i2c_init_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_init_params),
                FROTHY_FFI_VALUE_INT, "Stub I2C bus init on POSIX",
                prim_i2c_init, "( sda scl freq -- bus )"),
    POSIX_ENTRY("i2c.add-device", posix_i2c_add_device_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_add_device_params),
                FROTHY_FFI_VALUE_INT, "Stub I2C device add on POSIX",
                prim_i2c_add_device, "( bus addr speed -- device )"),
    POSIX_ENTRY("i2c.rm-device", posix_i2c_device_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_device_params),
                FROTHY_FFI_VALUE_NIL, "Stub I2C device remove on POSIX",
                prim_i2c_rm_device, "( device -- )"),
    POSIX_ENTRY("i2c.del-bus", posix_i2c_bus_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_bus_params),
                FROTHY_FFI_VALUE_NIL, "Stub I2C bus delete on POSIX",
                prim_i2c_del_bus, "( bus -- )"),
    POSIX_ENTRY("i2c.probe", posix_i2c_probe_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_probe_params),
                FROTHY_FFI_VALUE_INT, "Stub I2C probe on POSIX",
                prim_i2c_probe, "( bus addr -- flag )"),
    POSIX_ENTRY("i2c.write-byte", posix_i2c_write_byte_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_write_byte_params),
                FROTHY_FFI_VALUE_NIL, "Stub I2C write byte on POSIX",
                prim_i2c_write_byte, "( device byte -- )"),
    POSIX_ENTRY("i2c.read-byte", posix_i2c_device_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_device_params),
                FROTHY_FFI_VALUE_INT, "Stub I2C read byte on POSIX",
                prim_i2c_read_byte, "( device -- byte )"),
    POSIX_ENTRY("i2c.write-reg", posix_i2c_write_reg_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_write_reg_params),
                FROTHY_FFI_VALUE_NIL, "Stub I2C write register on POSIX",
                prim_i2c_write_reg, "( byte device reg -- )"),
    POSIX_ENTRY("i2c.read-reg", posix_i2c_read_reg_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_read_reg_params),
                FROTHY_FFI_VALUE_INT, "Stub I2C read register on POSIX",
                prim_i2c_read_reg, "( device reg -- byte )"),
    POSIX_ENTRY("i2c.read-reg16", posix_i2c_read_reg_params,
                FROTHY_FFI_PARAM_COUNT(posix_i2c_read_reg_params),
                FROTHY_FFI_VALUE_INT, "Stub I2C read 16-bit register on POSIX",
                prim_i2c_read_reg16, "( device reg -- word )"),
    POSIX_ENTRY("uart.init", posix_uart_init_params,
                FROTHY_FFI_PARAM_COUNT(posix_uart_init_params),
                FROTHY_FFI_VALUE_INT, "Stub UART init on POSIX",
                prim_uart_init, "( tx rx baud -- uart )"),
    POSIX_ENTRY("uart.write", posix_uart_write_params,
                FROTHY_FFI_PARAM_COUNT(posix_uart_write_params),
                FROTHY_FFI_VALUE_NIL, "Stub UART write byte on POSIX",
                prim_uart_write, "( byte uart -- )"),
    POSIX_ENTRY("uart.read", posix_uart_params,
                FROTHY_FFI_PARAM_COUNT(posix_uart_params), FROTHY_FFI_VALUE_INT,
                "Stub UART read byte on POSIX", prim_uart_read,
                "( uart -- byte )"),
    POSIX_ENTRY("uart.key?", posix_uart_params,
                FROTHY_FFI_PARAM_COUNT(posix_uart_params), FROTHY_FFI_VALUE_INT,
                "Stub UART key? on POSIX (always true)",
                prim_uart_available, "( uart -- flag )"),
    {0},
};
