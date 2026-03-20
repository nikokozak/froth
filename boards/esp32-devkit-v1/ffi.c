/* TODO: ESP32 DevKit V1 board FFI bindings */

#include "ffi.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
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

/*----------------- LEDC FUNCTIONS -----------------*/

FROTH_FFI(esp32_ledc_timer_config, "ledc.timer-config",
          "( speed_mode timer freq resolution -- )",
          "LEDC timer configuration.") {
  FROTH_POP(resolution);
  FROTH_POP(freq);
  FROTH_POP(timer);
  FROTH_POP(speed_mode);
  esp_err_t err =
      ledc_timer_config(&(ledc_timer_config_t){.speed_mode = speed_mode,
                                               .timer_num = timer,
                                               .freq_hz = freq,
                                               .duty_resolution = resolution,
                                               .clk_cfg = LEDC_AUTO_CLK,
                                               .deconfigure = false});

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_channel_config, "ledc.channel-config",
          "( pin speed_mode channel timer duty -- )",
          "LEDC channel configuration") {
  FROTH_POP(duty);
  FROTH_POP(timer);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  FROTH_POP(gpio_num);
  esp_err_t err = ledc_channel_config(&(ledc_channel_config_t){
      .speed_mode = speed_mode,
      .channel = channel,
      .timer_sel = timer,
      .gpio_num = gpio_num,
      .duty = duty,
      .flags = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .hpoint = 0,
      .deconfigure = false,
  });

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_set_duty, "ledc.set-duty",
          "( speed_mode channel duty -- )",
          "Set LEDC duty. Call ledc.update_duty after to apply.") {
  FROTH_POP(duty);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_set_duty(speed_mode, channel, duty);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_update_duty, "ledc.update-duty",
          "( speed_mode channel -- )", "Apply LEDC duty change") {
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_update_duty(speed_mode, channel);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_get_duty, "ledc.get-duty",
          "( speed_mode channel -- duty )", "Get LEDC duty") {
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  froth_cell_t duty = ledc_get_duty(speed_mode, channel);

  if (duty == LEDC_ERR_DUTY) {
    return FROTH_ERROR_IO;
  }

  FROTH_PUSH(duty);
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_set_frequency, "ledc.set-freq",
          "( speed_mode timer freq -- )",
          "Set LEDC frequency. Call ledc.update_duty after to apply.") {
  FROTH_POP(freq);
  FROTH_POP(timer);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_set_freq(speed_mode, timer, freq);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_get_frequency, "ledc.get-freq",
          "( speed_mode timer -- freq )", "Get LEDC frequency") {
  FROTH_POP(timer);
  FROTH_POP(speed_mode);
  uint32_t freq = ledc_get_freq(speed_mode, timer);

  if (freq == 0) { // Error is explicitly considered an error.
    return FROTH_ERROR_IO;
  }

  FROTH_PUSH(freq);
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_stop, "ledc.stop", "( speed_mode channel idle_level -- )",
          "Stop LEDC output") {
  FROTH_POP(idle_level);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_stop(speed_mode, channel, idle_level);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_fade_func_install, "ledc.fade-install", "( -- )",
          "Install LEDC fade function") {
  esp_err_t err = ledc_fade_func_install(0);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_fade_func_uninstall, "ledc.fade-uninstall", "( -- )",
          "Uninstall LEDC fade function") {
  ledc_fade_func_uninstall();

  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_fade_with_time, "ledc.fade-with-time",
          "( speed_mode channel target_duty time_ms -- )", "Start LEDC Fade.") {
  FROTH_POP(time_ms);
  FROTH_POP(target_duty);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err =
      ledc_set_fade_with_time(speed_mode, channel, target_duty, time_ms);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI(esp32_ledc_fade_start, "ledc.fade-start",
          "( speed_mode channel fade_mode -- )",
          "Start LEDC Fade. Call ledc.update_duty after to apply.") {
  FROTH_POP(fade_mode);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_fade_start(speed_mode, channel, fade_mode);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_BOARD_BEGIN(froth_board_bindings)
FROTH_BIND(esp32_gpio_mode), FROTH_BIND(esp32_gpio_read),
    FROTH_BIND(esp32_gpio_write), FROTH_BIND(esp32_ms),
    FROTH_BIND(esp32_ledc_timer_config), FROTH_BIND(esp32_ledc_channel_config),
    FROTH_BIND(esp32_ledc_set_duty), FROTH_BIND(esp32_ledc_update_duty),
    FROTH_BIND(esp32_ledc_get_duty), FROTH_BIND(esp32_ledc_set_frequency),
    FROTH_BIND(esp32_ledc_get_frequency), FROTH_BIND(esp32_ledc_stop),
    FROTH_BIND(esp32_ledc_fade_func_install),
    FROTH_BIND(esp32_ledc_fade_func_uninstall),
    FROTH_BIND(esp32_ledc_fade_time_start),
    FROTH_BIND(esp32_ledc_fade_start), FROTH_BOARD_END
