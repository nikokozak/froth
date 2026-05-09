/* ESP-IDF target-level FFI bindings shared by all ESP32 board profiles. */
#include "frothy_control.h"
#include "frothy_ffi.h"
#include "platform.h"

#include <stddef.h>

#define ESP_IDF_TARGET_UNUSED_CALLBACK_CONTEXT()                              \
  do {                                                                         \
    (void)runtime;                                                             \
    (void)context;                                                             \
    (void)arg_count;                                                           \
  } while (0)

static froth_error_t esp_idf_console_info(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  platform_console_uart_info_t info;

  ESP_IDF_TARGET_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  FROTH_TRY(platform_console_uart_info(&info));

  FROTH_TRY(frothy_ffi_emit_string("console uart"));
  FROTH_TRY(frothy_ffi_emit_string(frothy_ffi_format_number(info.port)));
  FROTH_TRY(frothy_ffi_emit_string(" tx="));
  FROTH_TRY(frothy_ffi_emit_string(frothy_ffi_format_number(info.tx)));
  FROTH_TRY(frothy_ffi_emit_string(" rx="));
  FROTH_TRY(frothy_ffi_emit_string(frothy_ffi_format_number(info.rx)));
  FROTH_TRY(frothy_ffi_emit_string(" baud="));
  FROTH_TRY(frothy_ffi_emit_string(frothy_ffi_format_number(info.baud)));
  FROTH_TRY(frothy_ffi_emit_string("\n"));
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp_idf_console_default(frothy_runtime_t *runtime,
                                             const void *context,
                                             const frothy_value_t *args,
                                             size_t arg_count,
                                             frothy_value_t *out) {
  ESP_IDF_TARGET_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  if (frothy_control_active()) {
    return FROTH_ERROR_BUSY;
  }

  FROTH_TRY(platform_console_uart_default());
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp_idf_console_uart_bind(frothy_runtime_t *runtime,
                                               const void *context,
                                               const frothy_value_t *args,
                                               size_t arg_count,
                                               frothy_value_t *out) {
  int32_t port = 0;
  int32_t tx = 0;
  int32_t rx = 0;
  int32_t baud = 0;

  ESP_IDF_TARGET_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &port));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &tx));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &rx));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &baud));

  if (frothy_control_active()) {
    return FROTH_ERROR_BUSY;
  }

  FROTH_TRY(platform_console_uart_bind(port, tx, rx, baud));
  return frothy_ffi_return_nil(out);
}

static const frothy_ffi_param_t esp_idf_console_uart_params[] = {
    FROTHY_FFI_PARAM_INT("port"),
    FROTHY_FFI_PARAM_INT("tx"),
    FROTHY_FFI_PARAM_INT("rx"),
    FROTHY_FFI_PARAM_INT("baud"),
};

#define ESP_IDF_TARGET_ENTRY(name_text, params_value, arity_value,             \
                             result_value, help_text, callback_value,          \
                             effect_text)                                      \
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

const frothy_ffi_entry_t frothy_target_bindings[] = {
    ESP_IDF_TARGET_ENTRY("console.info", NULL, 0, FROTHY_FFI_VALUE_NIL,
                         "Print the active console UART route.",
                         esp_idf_console_info, "( -- )"),
    ESP_IDF_TARGET_ENTRY("console.default!", NULL, 0, FROTHY_FFI_VALUE_NIL,
                         "Restore the default console UART route.",
                         esp_idf_console_default, "( -- )"),
    ESP_IDF_TARGET_ENTRY("console.uart!", esp_idf_console_uart_params,
                         FROTHY_FFI_PARAM_COUNT(esp_idf_console_uart_params),
                         FROTHY_FFI_VALUE_NIL,
                         "Rebind the active console to a UART route.",
                         esp_idf_console_uart_bind,
                         "( port tx rx baud -- )"),
    {0},
};
