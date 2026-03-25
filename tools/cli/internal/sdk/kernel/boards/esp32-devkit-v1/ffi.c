/* TODO: ESP32 DevKit V1 board FFI bindings */

#include "ffi.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "froth_types.h"

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
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {.output_invert = 0},
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

/* -----------------  I2C BINDINGS --------------------- */

#define I2C_MAX_BUSES 2
#define I2C_MAX_DEVICES 8
static i2c_master_bus_handle_t bus_handles[I2C_MAX_BUSES];
static i2c_master_dev_handle_t dev_handles[I2C_MAX_DEVICES];

FROTH_FFI(esp32_i2c_init, "i2c.init", "( sda scl freq -- bus )",
          "Initialize an I2C master bus. Returns a bus handle (0-1).") {
  FROTH_POP(freq);
  FROTH_POP(scl);
  FROTH_POP(sda);

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
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS; /* no free bus slot */
}

FROTH_FFI(esp32_i2c_add_device, "i2c.add-device",
          "( bus addr speed -- device )",
          "Add an I2C device to a bus. Returns a device handle (0-7).") {
  FROTH_POP(speed);
  FROTH_POP(addr);
  FROTH_POP(bus);

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
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS; /* no free device slot */
}

FROTH_FFI(esp32_i2c_rm_device, "i2c.rm-device", "( device -- )",
          "Remove an I2C device and release its handle.") {
  FROTH_POP(idx);

  if (idx < 0 || idx >= I2C_MAX_DEVICES || dev_handles[idx] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_master_bus_rm_device(dev_handles[idx]);
  dev_handles[idx] = NULL;
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_del_bus, "i2c.del-bus", "( bus -- )",
          "Delete an I2C master bus and release its handle.") {
  FROTH_POP(idx);

  if (idx < 0 || idx >= I2C_MAX_BUSES || bus_handles[idx] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_del_master_bus(bus_handles[idx]);
  bus_handles[idx] = NULL;
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_probe, "i2c.probe", "( bus addr -- flag )",
          "Probe for a device at addr. Returns true (-1) or false (0).") {
  FROTH_POP(addr);
  FROTH_POP(bus);

  if (bus < 0 || bus >= I2C_MAX_BUSES || bus_handles[bus] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_master_probe(bus_handles[bus], addr, 100);
  FROTH_PUSH(err == ESP_OK ? -1 : 0);
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_write_byte, "i2c.write-byte", "( device byte -- )",
          "Transmit one byte to an I2C device.") {
  FROTH_POP(byte);
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[1] = {(uint8_t)byte};
  esp_err_t err = i2c_master_transmit(dev_handles[dev], buf, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_read_byte, "i2c.read-byte", "( device -- byte )",
          "Receive one byte from an I2C device.") {
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[1] = {0};
  esp_err_t err = i2c_master_receive(dev_handles[dev], buf, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  FROTH_PUSH(buf[0]);
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_write_reg, "i2c.write-reg", "( byte device reg -- )",
          "Write a byte to a register on an I2C device.") {
  FROTH_POP(reg);
  FROTH_POP(dev);
  FROTH_POP(byte);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[2] = {(uint8_t)reg, (uint8_t)byte};
  esp_err_t err = i2c_master_transmit(dev_handles[dev], buf, 2, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_read_reg, "i2c.read-reg", "( device reg -- byte )",
          "Read one byte from a register on an I2C device.") {
  FROTH_POP(reg);
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t tx[1] = {(uint8_t)reg};
  uint8_t rx[1] = {0};
  esp_err_t err =
      i2c_master_transmit_receive(dev_handles[dev], tx, 1, rx, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  FROTH_PUSH(rx[0]);
  return FROTH_OK;
}

FROTH_FFI(esp32_i2c_read_reg16, "i2c.read-reg16", "( device reg -- word )",
          "Read two bytes (big-endian) from a register on an I2C device.") {
  FROTH_POP(reg);
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t tx[1] = {(uint8_t)reg};
  uint8_t rx[2] = {0, 0};
  esp_err_t err =
      i2c_master_transmit_receive(dev_handles[dev], tx, 1, rx, 2, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  froth_cell_t word = ((froth_cell_t)rx[0] << 8) | rx[1];
  FROTH_PUSH(word);
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
    FROTH_BIND(esp32_ledc_fade_with_time), FROTH_BIND(esp32_ledc_fade_start),
    FROTH_BIND(esp32_i2c_init), FROTH_BIND(esp32_i2c_add_device),
    FROTH_BIND(esp32_i2c_rm_device), FROTH_BIND(esp32_i2c_del_bus),
    FROTH_BIND(esp32_i2c_probe), FROTH_BIND(esp32_i2c_write_byte),
    FROTH_BIND(esp32_i2c_read_byte), FROTH_BIND(esp32_i2c_write_reg),
    FROTH_BIND(esp32_i2c_read_reg), FROTH_BIND(esp32_i2c_read_reg16),
    FROTH_BOARD_END
