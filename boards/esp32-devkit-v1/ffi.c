/* Maintained ESP32 DevKit V1 board FFI bindings. */

#include "ffi.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "froth_types.h"
#include "froth_vm.h"
#include "frothy_ffi.h"
#include "platform.h"

static froth_error_t throw_program_interrupted(froth_vm_t *froth_vm) {
  froth_vm->interrupted = 0;
  return FROTH_ERROR_PROGRAM_INTERRUPTED;
}

static froth_error_t poll_interruptible_wait(froth_vm_t *froth_vm) {
  frothy_ffi_poll(froth_vm);
  if (froth_vm->interrupted) {
    return throw_program_interrupted(froth_vm);
  }
  return FROTH_OK;
}

#define FROTH_BOARD_ADC_ATTEN ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t esp32_adc1_handle;
static uint8_t esp32_gpio_output_shadow_valid[GPIO_NUM_MAX];
static froth_cell_t esp32_gpio_output_shadow_levels[GPIO_NUM_MAX];
static uint32_t esp32_random_state = 1;

static bool esp32_adc1_channel_for_pin(froth_cell_t pin,
                                       adc_channel_t *channel_out) {
  switch (pin) {
  case 32:
    *channel_out = ADC_CHANNEL_4;
    return true;
  case 33:
    *channel_out = ADC_CHANNEL_5;
    return true;
  case 34:
    *channel_out = ADC_CHANNEL_6;
    return true;
  case 35:
    *channel_out = ADC_CHANNEL_7;
    return true;
  case 36:
    *channel_out = ADC_CHANNEL_0;
    return true;
  case 37:
    *channel_out = ADC_CHANNEL_1;
    return true;
  case 38:
    *channel_out = ADC_CHANNEL_2;
    return true;
  case 39:
    *channel_out = ADC_CHANNEL_3;
    return true;
  default:
    return false;
  }
}

static froth_error_t esp32_adc1_ensure(adc_oneshot_unit_handle_t *out) {
  if (esp32_adc1_handle == NULL) {
    const adc_oneshot_unit_init_cfg_t init = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    if (adc_oneshot_new_unit(&init, &esp32_adc1_handle) != ESP_OK) {
      return FROTH_ERROR_IO;
    }
  }

  *out = esp32_adc1_handle;
  return FROTH_OK;
}

static froth_error_t esp32_adc1_read_channel(adc_channel_t channel,
                                             int *sample_out) {
  adc_oneshot_unit_handle_t handle = NULL;
  const adc_oneshot_chan_cfg_t config = {
      .atten = FROTH_BOARD_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_12,
  };

  FROTH_TRY(esp32_adc1_ensure(&handle));
  if (adc_oneshot_config_channel(handle, channel, &config) != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  if (adc_oneshot_read(handle, channel, sample_out) != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

static bool esp32_gpio_pin_valid(froth_cell_t pin) {
  return pin >= 0 && GPIO_IS_VALID_GPIO((gpio_num_t)pin);
}

#define ESP32_UNUSED_CALLBACK_CONTEXT()                                        \
  do {                                                                         \
    (void)runtime;                                                             \
    (void)context;                                                             \
    (void)arg_count;                                                           \
  } while (0)

static froth_error_t esp32_gpio_mode(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;
  int32_t mode = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &mode));

  if (!esp32_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  esp_err_t err =
      gpio_set_direction(pin, mode == 1 ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  if (mode == 1) {
    esp32_gpio_output_shadow_valid[pin] = 1;
    esp32_gpio_output_shadow_levels[pin] = gpio_get_level(pin) ? 1 : 0;
  } else {
    esp32_gpio_output_shadow_valid[pin] = 0;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_gpio_write(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;
  int32_t level = 0;
  int32_t normalized = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &level));
  normalized = level ? 1 : 0;

  if (!esp32_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  esp_err_t err = gpio_set_level(pin, normalized);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  esp32_gpio_output_shadow_valid[pin] = 1;
  esp32_gpio_output_shadow_levels[pin] = normalized;
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_gpio_read(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));

  if (!esp32_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

  int32_t level = 0;
  if (esp32_gpio_output_shadow_valid[pin]) {
    level = (int32_t)esp32_gpio_output_shadow_levels[pin];
  } else {
    level = gpio_get_level(pin) ? 1 : 0;
  }
  return frothy_ffi_return_int(level, out);
}

static froth_error_t esp32_ms(frothy_runtime_t *runtime, const void *context,
                              const frothy_value_t *args, size_t arg_count,
                              frothy_value_t *out) {
  int32_t ms = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &ms));

  if (ms <= 0) {
    return frothy_ffi_return_nil(out);
  }

  while (ms > 0) {
    int32_t chunk = ms > 10 ? 10 : ms;
    vTaskDelay(pdMS_TO_TICKS(chunk));
    ms -= chunk;
    FROTH_TRY(poll_interruptible_wait(&froth_vm));
  }

  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_millis(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out) {
  ESP32_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  return frothy_ffi_return_int(
      (int32_t)frothy_ffi_wrap_uptime_ms(platform_uptime_ms()), out);
}

static froth_error_t esp32_adc_read(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  adc_channel_t channel;
  int sample = 0;
  int32_t pin = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));

  if (!esp32_adc1_channel_for_pin(pin, &channel)) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(esp32_adc1_read_channel(channel, &sample));

  return frothy_ffi_return_int(sample, out);
}

static froth_error_t esp32_random_seed(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  int32_t seed = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &seed));
  esp32_random_state = frothy_ffi_random_seed((uint32_t)seed);
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_random_seed_from_millis(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  ESP32_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  esp32_random_state = frothy_ffi_random_seed(platform_uptime_ms());
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_random_next(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  ESP32_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  return frothy_ffi_return_int(frothy_ffi_random_next_int(&esp32_random_state),
                               out);
}

static froth_error_t esp32_random_below(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t limit = 0;
  uint32_t value = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &limit));
  if (limit <= 0) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_TRY(frothy_ffi_random_below(&esp32_random_state, (uint32_t)limit,
                                    &value));
  return frothy_ffi_return_int((int32_t)value, out);
}

static froth_error_t esp32_random_range(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t lo = 0;
  int32_t hi = 0;
  uint32_t offset = 0;
  int64_t span = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
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
      frothy_ffi_random_below(&esp32_random_state, (uint32_t)span, &offset));
  return frothy_ffi_return_int((int32_t)((int64_t)lo + (int64_t)offset), out);
}

/*----------------- LEDC FUNCTIONS -----------------*/

static froth_error_t esp32_ledc_timer_config(frothy_runtime_t *runtime,
                                             const void *context,
                                             const frothy_value_t *args,
                                             size_t arg_count,
                                             frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t timer = 0;
  int32_t freq = 0;
  int32_t resolution = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &timer));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &freq));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &resolution));
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
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_channel_config(frothy_runtime_t *runtime,
                                               const void *context,
                                               const frothy_value_t *args,
                                               size_t arg_count,
                                               frothy_value_t *out) {
  int32_t gpio_num = 0;
  int32_t speed_mode = 0;
  int32_t channel = 0;
  int32_t timer = 0;
  int32_t duty = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &gpio_num));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &channel));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &timer));
  FROTH_TRY(frothy_ffi_expect_int(args, 4, &duty));
  esp_err_t err = ledc_channel_config(&(ledc_channel_config_t){
      .speed_mode = speed_mode,
      .channel = channel,
      .timer_sel = timer,
      .gpio_num = gpio_num,
      .duty = duty,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {.output_invert = 0},
  });

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_set_duty(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t channel = 0;
  int32_t duty = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &channel));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &duty));
  esp_err_t err = ledc_set_duty(speed_mode, channel, duty);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_update_duty(frothy_runtime_t *runtime,
                                            const void *context,
                                            const frothy_value_t *args,
                                            size_t arg_count,
                                            frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t channel = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &channel));
  esp_err_t err = ledc_update_duty(speed_mode, channel);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_get_duty(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t channel = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &channel));
  uint32_t duty = ledc_get_duty(speed_mode, channel);

  if (duty == LEDC_ERR_DUTY) {
    return FROTH_ERROR_IO;
  }

  return frothy_ffi_return_int((int32_t)duty, out);
}

static froth_error_t esp32_ledc_set_frequency(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t timer = 0;
  int32_t freq = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &timer));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &freq));
  esp_err_t err = ledc_set_freq(speed_mode, timer, freq);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_get_frequency(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t timer = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &timer));
  uint32_t freq = ledc_get_freq(speed_mode, timer);

  if (freq == 0) { // Error is explicitly considered an error.
    return FROTH_ERROR_IO;
  }

  return frothy_ffi_return_int((int32_t)freq, out);
}

static froth_error_t esp32_ledc_stop(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t channel = 0;
  int32_t idle_level = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &channel));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &idle_level));
  esp_err_t err = ledc_stop(speed_mode, channel, idle_level);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_fade_func_install(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  ESP32_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  esp_err_t err = ledc_fade_func_install(0);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_fade_func_uninstall(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  ESP32_UNUSED_CALLBACK_CONTEXT();
  (void)args;
  ledc_fade_func_uninstall();

  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_fade_with_time(frothy_runtime_t *runtime,
                                               const void *context,
                                               const frothy_value_t *args,
                                               size_t arg_count,
                                               frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t channel = 0;
  int32_t target_duty = 0;
  int32_t time_ms = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &channel));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &target_duty));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &time_ms));
  esp_err_t err =
      ledc_set_fade_with_time(speed_mode, channel, target_duty, time_ms);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_ledc_fade_start(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  int32_t speed_mode = 0;
  int32_t channel = 0;
  int32_t fade_mode = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &speed_mode));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &channel));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &fade_mode));
  esp_err_t err = ledc_fade_start(speed_mode, channel, fade_mode);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

/* -----------------  I2C BINDINGS --------------------- */

#define I2C_MAX_BUSES 2
#define I2C_MAX_DEVICES 8
static i2c_master_bus_handle_t bus_handles[I2C_MAX_BUSES];
static i2c_master_dev_handle_t dev_handles[I2C_MAX_DEVICES];

static froth_error_t esp32_i2c_init(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count, frothy_value_t *out) {
  int32_t sda = 0;
  int32_t scl = 0;
  int32_t freq = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &sda));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &scl));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &freq));

  for (int i = 0; i < I2C_MAX_BUSES; i++) {
    if (bus_handles[i] != NULL)
      continue;

    i2c_master_bus_config_t config = {
        .i2c_port = -1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = 1, .allow_pd = 0},
    };

    i2c_master_bus_handle_t handle;
    esp_err_t err = i2c_new_master_bus(&config, &handle);
    if (err != ESP_OK)
      return FROTH_ERROR_IO;

    bus_handles[i] = handle;
    return frothy_ffi_return_int(i, out);
  }

  return FROTH_ERROR_BOUNDS; /* no free bus slot */
}

static froth_error_t esp32_i2c_add_device(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  int32_t bus = 0;
  int32_t addr = 0;
  int32_t speed = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &bus));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &addr));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &speed));

  if (bus < 0 || bus >= I2C_MAX_BUSES || bus_handles[bus] == NULL)
    return FROTH_ERROR_BOUNDS;

  for (int i = 0; i < I2C_MAX_DEVICES; i++) {
    if (dev_handles[i] != NULL)
      continue;

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = 0},
    };

    i2c_master_dev_handle_t handle;
    esp_err_t err =
        i2c_master_bus_add_device(bus_handles[bus], &config, &handle);
    if (err != ESP_OK)
      return FROTH_ERROR_IO;

    dev_handles[i] = handle;
    return frothy_ffi_return_int(i, out);
  }

  return FROTH_ERROR_BOUNDS; /* no free device slot */
}

static froth_error_t esp32_i2c_rm_device(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t idx = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &idx));

  if (idx < 0 || idx >= I2C_MAX_DEVICES || dev_handles[idx] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_master_bus_rm_device(dev_handles[idx]);
  dev_handles[idx] = NULL;
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_i2c_del_bus(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  int32_t idx = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &idx));

  if (idx < 0 || idx >= I2C_MAX_BUSES || bus_handles[idx] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_del_master_bus(bus_handles[idx]);
  bus_handles[idx] = NULL;
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_i2c_probe(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t bus = 0;
  int32_t addr = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &bus));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &addr));

  if (bus < 0 || bus >= I2C_MAX_BUSES || bus_handles[bus] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_master_probe(bus_handles[bus], addr, 100);
  return frothy_ffi_return_int(err == ESP_OK ? -1 : 0, out);
}

static froth_error_t esp32_i2c_write_byte(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  int32_t dev = 0;
  int32_t byte = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &dev));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &byte));

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[1] = {(uint8_t)byte};
  esp_err_t err = i2c_master_transmit(dev_handles[dev], buf, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_i2c_read_byte(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t dev = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &dev));

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[1] = {0};
  esp_err_t err = i2c_master_receive(dev_handles[dev], buf, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  return frothy_ffi_return_int(buf[0], out);
}

static froth_error_t esp32_i2c_write_reg(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t byte = 0;
  int32_t dev = 0;
  int32_t reg = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &byte));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &dev));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &reg));

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[2] = {(uint8_t)reg, (uint8_t)byte};
  esp_err_t err = i2c_master_transmit(dev_handles[dev], buf, 2, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_i2c_read_reg(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t dev = 0;
  int32_t reg = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &dev));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &reg));

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t tx[1] = {(uint8_t)reg};
  uint8_t rx[1] = {0};
  esp_err_t err =
      i2c_master_transmit_receive(dev_handles[dev], tx, 1, rx, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  return frothy_ffi_return_int(rx[0], out);
}

static froth_error_t esp32_i2c_read_reg16(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  int32_t dev = 0;
  int32_t reg = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &dev));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &reg));

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t tx[1] = {(uint8_t)reg};
  uint8_t rx[2] = {0, 0};
  esp_err_t err =
      i2c_master_transmit_receive(dev_handles[dev], tx, 1, rx, 2, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  int32_t word = ((int32_t)rx[0] << 8) | rx[1];
  return frothy_ffi_return_int(word, out);
}

/* ----------------- UART BINDINGS --------------------- */

#define UART_MAX_PORTS 2
static const uart_port_t uart_ports[UART_MAX_PORTS] = {UART_NUM_1, UART_NUM_2};
static uint8_t uart_in_use[UART_MAX_PORTS];
static int uart_tx_pins[UART_MAX_PORTS];
static int uart_rx_pins[UART_MAX_PORTS];

void froth_board_reset_runtime_state(void) {
  for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
    if (esp32_gpio_output_shadow_valid[pin]) {
      (void)gpio_set_level((gpio_num_t)pin, 0);
    }
    esp32_gpio_output_shadow_valid[pin] = 0;
    esp32_gpio_output_shadow_levels[pin] = 0;
  }

  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (uart_in_use[i]) {
      (void)uart_driver_delete(uart_ports[i]);
    }
    uart_in_use[i] = 0;
    uart_tx_pins[i] = -1;
    uart_rx_pins[i] = -1;
  }

  esp32_random_state = frothy_ffi_random_seed(1);
}

static bool aux_uart_conflicts_console(froth_cell_t tx, froth_cell_t rx) {
  platform_console_uart_info_t info;

  if (platform_console_uart_info(&info) != FROTH_OK) {
    return false;
  }

  return info.tx == tx || info.tx == rx || info.rx == tx || info.rx == rx;
}

static bool aux_uart_port_conflicts_console(int index) {
  platform_console_uart_info_t info;

  if (platform_console_uart_info(&info) != FROTH_OK) {
    return false;
  }

  return info.port == uart_ports[index];
}

static bool console_route_conflicts_aux(froth_cell_t port, froth_cell_t tx,
                                        froth_cell_t rx) {
  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (!uart_in_use[i]) {
      continue;
    }

    if ((froth_cell_t)uart_ports[i] == port) {
      return true;
    }

    if (uart_tx_pins[i] == tx || uart_tx_pins[i] == rx ||
        uart_rx_pins[i] == tx || uart_rx_pins[i] == rx) {
      return true;
    }
  }

  return false;
}

static froth_error_t esp32_uart_init(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t tx = 0;
  int32_t rx = 0;
  int32_t baud = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &tx));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &rx));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &baud));

  if (aux_uart_conflicts_console(tx, rx)) {
    return FROTH_ERROR_BUSY;
  }

  uart_config_t config = {
      .baud_rate = (int)baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (aux_uart_port_conflicts_console(i)) {
      continue;
    }

    if (!uart_in_use[i]) {
      continue;
    }

    if (uart_tx_pins[i] != tx || uart_rx_pins[i] != rx) {
      continue;
    }

    esp_err_t err = uart_param_config(uart_ports[i], &config);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    err = uart_flush_input(uart_ports[i]);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    return frothy_ffi_return_int(i, out);
  }

  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (aux_uart_port_conflicts_console(i)) {
      continue;
    }

    if (uart_in_use[i]) {
      continue;
    }

    esp_err_t err = uart_driver_install(uart_ports[i], 256, 0, 0, NULL, 0);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    err = uart_param_config(uart_ports[i], &config);
    if (err != ESP_OK) {
      uart_driver_delete(uart_ports[i]);
      return FROTH_ERROR_IO;
    }

    err = uart_set_pin(uart_ports[i], tx, rx, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
      uart_driver_delete(uart_ports[i]);
      return FROTH_ERROR_IO;
    }

    err = uart_flush_input(uart_ports[i]);
    if (err != ESP_OK) {
      uart_driver_delete(uart_ports[i]);
      return FROTH_ERROR_IO;
    }

    uart_in_use[i] = 1;
    uart_tx_pins[i] = (int)tx;
    uart_rx_pins[i] = (int)rx;
    return frothy_ffi_return_int(i, out);
  }

  return FROTH_ERROR_BOUNDS;
}

static froth_error_t esp32_console_info(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  platform_console_uart_info_t info;

  ESP32_UNUSED_CALLBACK_CONTEXT();
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

static froth_error_t esp32_console_default(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  ESP32_UNUSED_CALLBACK_CONTEXT();
  (void)args;

  if (console_route_conflicts_aux(FROTH_BOARD_CONSOLE_DEFAULT_PORT,
                                  FROTH_BOARD_CONSOLE_DEFAULT_TX_PIN,
                                  FROTH_BOARD_CONSOLE_DEFAULT_RX_PIN)) {
    return FROTH_ERROR_BUSY;
  }

  FROTH_TRY(platform_console_uart_default());
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_console_uart_bind(frothy_runtime_t *runtime,
                                             const void *context,
                                             const frothy_value_t *args,
                                             size_t arg_count,
                                             frothy_value_t *out) {
  int32_t port = 0;
  int32_t tx = 0;
  int32_t rx = 0;
  int32_t baud = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &port));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &tx));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &rx));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &baud));

  if (console_route_conflicts_aux(port, tx, rx)) {
    return FROTH_ERROR_BUSY;
  }

  FROTH_TRY(platform_console_uart_bind(port, tx, rx, baud));
  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_uart_write(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count, frothy_value_t *out) {
  int32_t byte = 0;
  int32_t uart = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &byte));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &uart));

  if (uart < 0 || uart >= UART_MAX_PORTS || !uart_in_use[uart]) {
    return FROTH_ERROR_BOUNDS;
  }

  uint8_t byte_out = (uint8_t)(byte & 0xff);
  int written = uart_write_bytes(uart_ports[uart], &byte_out, 1);
  if (written != 1) {
    return FROTH_ERROR_IO;
  }

  return frothy_ffi_return_nil(out);
}

static froth_error_t esp32_uart_read(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  int32_t uart = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &uart));

  if (uart < 0 || uart >= UART_MAX_PORTS || !uart_in_use[uart]) {
    return FROTH_ERROR_BOUNDS;
  }

  while (1) {
    uint8_t in = 0;
    int read = uart_read_bytes(uart_ports[uart], &in, 1, pdMS_TO_TICKS(10));
    if (read < 0) {
      return FROTH_ERROR_IO;
    }
    if (read == 1) {
      return frothy_ffi_return_int(in, out);
    }

    FROTH_TRY(poll_interruptible_wait(&froth_vm));
  }
}

static froth_error_t esp32_uart_available(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  int32_t uart = 0;

  ESP32_UNUSED_CALLBACK_CONTEXT();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &uart));

  if (uart < 0 || uart >= UART_MAX_PORTS || !uart_in_use[uart]) {
    return FROTH_ERROR_BOUNDS;
  }

  size_t len = 0;
  froth_cell_t flag = 0;
  esp_err_t err = uart_get_buffered_data_len(uart_ports[uart], &len);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  if (len > 0) {
    flag = -1;
  }

  return frothy_ffi_return_int((int32_t)flag, out);
}

static const frothy_ffi_param_t esp32_pin_mode_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("mode"),
};

static const frothy_ffi_param_t esp32_pin_level_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("level"),
};

static const frothy_ffi_param_t esp32_pin_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
};

static const frothy_ffi_param_t esp32_delay_params[] = {
    FROTHY_FFI_PARAM_INT("ms"),
};

static const frothy_ffi_param_t esp32_random_seed_params[] = {
    FROTHY_FFI_PARAM_INT("seed"),
};

static const frothy_ffi_param_t esp32_random_below_params[] = {
    FROTHY_FFI_PARAM_INT("limit"),
};

static const frothy_ffi_param_t esp32_random_range_params[] = {
    FROTHY_FFI_PARAM_INT("lo"),
    FROTHY_FFI_PARAM_INT("hi"),
};

static const frothy_ffi_param_t esp32_ledc_timer_config_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("timer"),
    FROTHY_FFI_PARAM_INT("freq"),
    FROTHY_FFI_PARAM_INT("resolution"),
};

static const frothy_ffi_param_t esp32_ledc_channel_config_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("channel"),
    FROTHY_FFI_PARAM_INT("timer"),
    FROTHY_FFI_PARAM_INT("duty"),
};

static const frothy_ffi_param_t esp32_ledc_speed_channel_duty_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("channel"),
    FROTHY_FFI_PARAM_INT("duty"),
};

static const frothy_ffi_param_t esp32_ledc_speed_channel_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("channel"),
};

static const frothy_ffi_param_t esp32_ledc_speed_timer_freq_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("timer"),
    FROTHY_FFI_PARAM_INT("freq"),
};

static const frothy_ffi_param_t esp32_ledc_speed_timer_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("timer"),
};

static const frothy_ffi_param_t esp32_ledc_stop_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("channel"),
    FROTHY_FFI_PARAM_INT("idle_level"),
};

static const frothy_ffi_param_t esp32_ledc_fade_with_time_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("channel"),
    FROTHY_FFI_PARAM_INT("target_duty"),
    FROTHY_FFI_PARAM_INT("time_ms"),
};

static const frothy_ffi_param_t esp32_ledc_fade_start_params[] = {
    FROTHY_FFI_PARAM_INT("speed_mode"),
    FROTHY_FFI_PARAM_INT("channel"),
    FROTHY_FFI_PARAM_INT("fade_mode"),
};

static const frothy_ffi_param_t esp32_i2c_init_params[] = {
    FROTHY_FFI_PARAM_INT("sda"),
    FROTHY_FFI_PARAM_INT("scl"),
    FROTHY_FFI_PARAM_INT("freq"),
};

static const frothy_ffi_param_t esp32_i2c_add_device_params[] = {
    FROTHY_FFI_PARAM_INT("bus"),
    FROTHY_FFI_PARAM_INT("addr"),
    FROTHY_FFI_PARAM_INT("speed"),
};

static const frothy_ffi_param_t esp32_i2c_device_params[] = {
    FROTHY_FFI_PARAM_INT("device"),
};

static const frothy_ffi_param_t esp32_i2c_bus_params[] = {
    FROTHY_FFI_PARAM_INT("bus"),
};

static const frothy_ffi_param_t esp32_i2c_probe_params[] = {
    FROTHY_FFI_PARAM_INT("bus"),
    FROTHY_FFI_PARAM_INT("addr"),
};

static const frothy_ffi_param_t esp32_i2c_write_byte_params[] = {
    FROTHY_FFI_PARAM_INT("device"),
    FROTHY_FFI_PARAM_INT("byte"),
};

static const frothy_ffi_param_t esp32_i2c_write_reg_params[] = {
    FROTHY_FFI_PARAM_INT("byte"),
    FROTHY_FFI_PARAM_INT("device"),
    FROTHY_FFI_PARAM_INT("reg"),
};

static const frothy_ffi_param_t esp32_i2c_read_reg_params[] = {
    FROTHY_FFI_PARAM_INT("device"),
    FROTHY_FFI_PARAM_INT("reg"),
};

static const frothy_ffi_param_t esp32_console_uart_params[] = {
    FROTHY_FFI_PARAM_INT("port"),
    FROTHY_FFI_PARAM_INT("tx"),
    FROTHY_FFI_PARAM_INT("rx"),
    FROTHY_FFI_PARAM_INT("baud"),
};

static const frothy_ffi_param_t esp32_uart_init_params[] = {
    FROTHY_FFI_PARAM_INT("tx"),
    FROTHY_FFI_PARAM_INT("rx"),
    FROTHY_FFI_PARAM_INT("baud"),
};

static const frothy_ffi_param_t esp32_uart_write_params[] = {
    FROTHY_FFI_PARAM_INT("byte"),
    FROTHY_FFI_PARAM_INT("uart"),
};

static const frothy_ffi_param_t esp32_uart_params[] = {
    FROTHY_FFI_PARAM_INT("uart"),
};

#define ESP32_ENTRY(name_text, params_value, arity_value, result_value,        \
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
    ESP32_ENTRY("gpio.mode", esp32_pin_mode_params,
                FROTHY_FFI_PARAM_COUNT(esp32_pin_mode_params),
                FROTHY_FFI_VALUE_NIL, "Set pin mode (1=output).",
                esp32_gpio_mode, "( pin mode -- )"),
    ESP32_ENTRY("gpio.read", esp32_pin_params,
                FROTHY_FFI_PARAM_COUNT(esp32_pin_params), FROTHY_FFI_VALUE_INT,
                "Read the last written level for outputs, otherwise sample the live pin.",
                esp32_gpio_read, "( pin -- level )"),
    ESP32_ENTRY("gpio.write", esp32_pin_level_params,
                FROTHY_FFI_PARAM_COUNT(esp32_pin_level_params),
                FROTHY_FFI_VALUE_NIL, "Set pin level (1=high).",
                esp32_gpio_write, "( pin level -- )"),
    ESP32_ENTRY("ms", esp32_delay_params,
                FROTHY_FFI_PARAM_COUNT(esp32_delay_params),
                FROTHY_FFI_VALUE_NIL, "Sleep for a given amount of ms.",
                esp32_ms, "( ms -- )"),
    ESP32_ENTRY("millis", NULL, 0, FROTHY_FFI_VALUE_INT,
                "Return wrapped monotonic uptime in milliseconds.",
                esp32_millis, "( -- n )"),
    ESP32_ENTRY("adc.read", esp32_pin_params,
                FROTHY_FFI_PARAM_COUNT(esp32_pin_params), FROTHY_FFI_VALUE_INT,
                "Read a 12-bit ADC1 sample from a GPIO pin.", esp32_adc_read,
                "( pin -- value )"),
    ESP32_ENTRY("random.seed!", esp32_random_seed_params,
                FROTHY_FFI_PARAM_COUNT(esp32_random_seed_params),
                FROTHY_FFI_VALUE_NIL,
                "Seed the board pseudo-random generator.", esp32_random_seed,
                "( seed -- )"),
    ESP32_ENTRY("random.seedFromMillis!", NULL, 0, FROTHY_FFI_VALUE_NIL,
                "Seed the board pseudo-random generator from millis.",
                esp32_random_seed_from_millis, "( -- )"),
    ESP32_ENTRY("random.next", NULL, 0, FROTHY_FFI_VALUE_INT,
                "Return the next non-negative pseudo-random integer.",
                esp32_random_next, "( -- n )"),
    ESP32_ENTRY("random.below", esp32_random_below_params,
                FROTHY_FFI_PARAM_COUNT(esp32_random_below_params),
                FROTHY_FFI_VALUE_INT,
                "Return a pseudo-random integer in [0, limit).",
                esp32_random_below, "( limit -- n )"),
    ESP32_ENTRY("random.range", esp32_random_range_params,
                FROTHY_FFI_PARAM_COUNT(esp32_random_range_params),
                FROTHY_FFI_VALUE_INT,
                "Return a pseudo-random integer between lo and hi inclusive.",
                esp32_random_range, "( lo hi -- n )"),
    ESP32_ENTRY("ledc.timer-config", esp32_ledc_timer_config_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_timer_config_params),
                FROTHY_FFI_VALUE_NIL, "LEDC timer configuration.",
                esp32_ledc_timer_config,
                "( speed_mode timer freq resolution -- )"),
    ESP32_ENTRY("ledc.channel-config", esp32_ledc_channel_config_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_channel_config_params),
                FROTHY_FFI_VALUE_NIL, "LEDC channel configuration.",
                esp32_ledc_channel_config,
                "( pin speed_mode channel timer duty -- )"),
    ESP32_ENTRY("ledc.set-duty", esp32_ledc_speed_channel_duty_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_speed_channel_duty_params),
                FROTHY_FFI_VALUE_NIL,
                "Set LEDC duty. Call ledc.update_duty after to apply.",
                esp32_ledc_set_duty, "( speed_mode channel duty -- )"),
    ESP32_ENTRY("ledc.update-duty", esp32_ledc_speed_channel_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_speed_channel_params),
                FROTHY_FFI_VALUE_NIL, "Apply LEDC duty change.",
                esp32_ledc_update_duty, "( speed_mode channel -- )"),
    ESP32_ENTRY("ledc.get-duty", esp32_ledc_speed_channel_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_speed_channel_params),
                FROTHY_FFI_VALUE_INT, "Get LEDC duty.", esp32_ledc_get_duty,
                "( speed_mode channel -- duty )"),
    ESP32_ENTRY("ledc.set-freq", esp32_ledc_speed_timer_freq_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_speed_timer_freq_params),
                FROTHY_FFI_VALUE_NIL,
                "Set LEDC frequency. Call ledc.update_duty after to apply.",
                esp32_ledc_set_frequency, "( speed_mode timer freq -- )"),
    ESP32_ENTRY("ledc.get-freq", esp32_ledc_speed_timer_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_speed_timer_params),
                FROTHY_FFI_VALUE_INT, "Get LEDC frequency.",
                esp32_ledc_get_frequency, "( speed_mode timer -- freq )"),
    ESP32_ENTRY("ledc.stop", esp32_ledc_stop_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_stop_params),
                FROTHY_FFI_VALUE_NIL, "Stop LEDC output.", esp32_ledc_stop,
                "( speed_mode channel idle_level -- )"),
    ESP32_ENTRY("ledc.fade-install", NULL, 0, FROTHY_FFI_VALUE_NIL,
                "Install LEDC fade function.", esp32_ledc_fade_func_install,
                "( -- )"),
    ESP32_ENTRY("ledc.fade-uninstall", NULL, 0, FROTHY_FFI_VALUE_NIL,
                "Uninstall LEDC fade function.",
                esp32_ledc_fade_func_uninstall, "( -- )"),
    ESP32_ENTRY("ledc.fade-with-time", esp32_ledc_fade_with_time_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_fade_with_time_params),
                FROTHY_FFI_VALUE_NIL, "Start LEDC Fade.",
                esp32_ledc_fade_with_time,
                "( speed_mode channel target_duty time_ms -- )"),
    ESP32_ENTRY("ledc.fade-start", esp32_ledc_fade_start_params,
                FROTHY_FFI_PARAM_COUNT(esp32_ledc_fade_start_params),
                FROTHY_FFI_VALUE_NIL,
                "Start LEDC Fade. Call ledc.update_duty after to apply.",
                esp32_ledc_fade_start, "( speed_mode channel fade_mode -- )"),
    ESP32_ENTRY("i2c.init", esp32_i2c_init_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_init_params),
                FROTHY_FFI_VALUE_INT,
                "Initialize an I2C master bus. Returns a bus handle (0-1).",
                esp32_i2c_init, "( sda scl freq -- bus )"),
    ESP32_ENTRY("i2c.add-device", esp32_i2c_add_device_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_add_device_params),
                FROTHY_FFI_VALUE_INT,
                "Add an I2C device to a bus. Returns a device handle (0-7).",
                esp32_i2c_add_device, "( bus addr speed -- device )"),
    ESP32_ENTRY("i2c.rm-device", esp32_i2c_device_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_device_params),
                FROTHY_FFI_VALUE_NIL,
                "Remove an I2C device and release its handle.",
                esp32_i2c_rm_device, "( device -- )"),
    ESP32_ENTRY("i2c.del-bus", esp32_i2c_bus_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_bus_params),
                FROTHY_FFI_VALUE_NIL,
                "Delete an I2C master bus and release its handle.",
                esp32_i2c_del_bus, "( bus -- )"),
    ESP32_ENTRY("i2c.probe", esp32_i2c_probe_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_probe_params),
                FROTHY_FFI_VALUE_INT,
                "Probe for a device at addr. Returns true (-1) or false (0).",
                esp32_i2c_probe, "( bus addr -- flag )"),
    ESP32_ENTRY("i2c.write-byte", esp32_i2c_write_byte_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_write_byte_params),
                FROTHY_FFI_VALUE_NIL, "Transmit one byte to an I2C device.",
                esp32_i2c_write_byte, "( device byte -- )"),
    ESP32_ENTRY("i2c.read-byte", esp32_i2c_device_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_device_params),
                FROTHY_FFI_VALUE_INT, "Receive one byte from an I2C device.",
                esp32_i2c_read_byte, "( device -- byte )"),
    ESP32_ENTRY("i2c.write-reg", esp32_i2c_write_reg_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_write_reg_params),
                FROTHY_FFI_VALUE_NIL,
                "Write a byte to a register on an I2C device.",
                esp32_i2c_write_reg, "( byte device reg -- )"),
    ESP32_ENTRY("i2c.read-reg", esp32_i2c_read_reg_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_read_reg_params),
                FROTHY_FFI_VALUE_INT,
                "Read one byte from a register on an I2C device.",
                esp32_i2c_read_reg, "( device reg -- byte )"),
    ESP32_ENTRY("i2c.read-reg16", esp32_i2c_read_reg_params,
                FROTHY_FFI_PARAM_COUNT(esp32_i2c_read_reg_params),
                FROTHY_FFI_VALUE_INT,
                "Read two bytes (big-endian) from a register on an I2C device.",
                esp32_i2c_read_reg16, "( device reg -- word )"),
    ESP32_ENTRY("console.info", NULL, 0, FROTHY_FFI_VALUE_NIL,
                "Print the active console UART route.", esp32_console_info,
                "( -- )"),
    ESP32_ENTRY("console.default!", NULL, 0, FROTHY_FFI_VALUE_NIL,
                "Restore the default console UART route.",
                esp32_console_default, "( -- )"),
    ESP32_ENTRY("console.uart!", esp32_console_uart_params,
                FROTHY_FFI_PARAM_COUNT(esp32_console_uart_params),
                FROTHY_FFI_VALUE_NIL,
                "Rebind the active console to a UART route.",
                esp32_console_uart_bind, "( port tx rx baud -- )"),
    ESP32_ENTRY("uart.init", esp32_uart_init_params,
                FROTHY_FFI_PARAM_COUNT(esp32_uart_init_params),
                FROTHY_FFI_VALUE_INT,
                "Initialize an auxiliary UART. Returns a UART handle (0-1).",
                esp32_uart_init, "( tx rx baud -- uart )"),
    ESP32_ENTRY("uart.write", esp32_uart_write_params,
                FROTHY_FFI_PARAM_COUNT(esp32_uart_write_params),
                FROTHY_FFI_VALUE_NIL, "Write one byte to an auxiliary UART.",
                esp32_uart_write, "( byte uart -- )"),
    ESP32_ENTRY("uart.read", esp32_uart_params,
                FROTHY_FFI_PARAM_COUNT(esp32_uart_params), FROTHY_FFI_VALUE_INT,
                "Read one byte from an auxiliary UART.", esp32_uart_read,
                "( uart -- byte )"),
    ESP32_ENTRY("uart.key?", esp32_uart_params,
                FROTHY_FFI_PARAM_COUNT(esp32_uart_params), FROTHY_FFI_VALUE_INT,
                "Returns true (-1) if at least one byte is in RX buffer, (0) otherwise.",
                esp32_uart_available, "( uart -- flag )"),
    {0},
};
