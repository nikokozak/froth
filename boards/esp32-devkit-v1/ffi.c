/* TODO: ESP32 DevKit V1 board FFI bindings */

#include "ffi.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

FROTH_FFI(esp32_gpio_mode, "gpio.mode", "( pin mode -- )",
          "Set pin mode (1=output)") {
  FROTH_POP(mode);
  FROTH_POP(pin);

  esp_err_t err =
      gpio_set_direction(pin, mode == 1 ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_gpio_write, "gpio.write", "( pin level -- )",
          "Set pin level (1=high)") {
  FROTH_POP(level);
  FROTH_POP(pin);

  esp_err_t err = gpio_set_level(pin, level);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(
    esp32_gpio_read, "gpio.read", "( pin -- level )",
    "Read pin level. Pin mode MUST be set, otherwise will always return 0.") {
  FROTH_POP(pin);

  froth_cell_t level = gpio_get_level(pin);
  FROTH_PUSH(level);
  return FROTH_OK;
}

FROTH_FFI(esp32_ms, "ms", "( ms -- )", "Sleep for a given amount of ms.") {
  FROTH_POP(ms);
  // Convert to ms
  vTaskDelay(pdMS_TO_TICKS(ms)); // sleep
  return FROTH_OK;
}

FROTH_BOARD_BEGIN(froth_board_bindings)
FROTH_BIND(esp32_gpio_mode), FROTH_BIND(esp32_gpio_read),
    FROTH_BIND(esp32_gpio_write), FROTH_BIND(esp32_ms), FROTH_BOARD_END
