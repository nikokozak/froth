/* TODO: ESP-IDF platform implementation */
#include "platform.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_vfs_dev.h" /* uart_vfs_dev_use_driver */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "froth_types.h"
#include "froth_vm.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>

void platform_delay_ms(froth_cell_u_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms)); // sleep
}

froth_error_t platform_init(void) {
  // Disable stdio buffering before installing driver.
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  // Install UART driver so fgetc(stdin) blocks properly.
  esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  uart_flush(UART_NUM_0); // Get rid of dirty RX data
  vTaskDelay(pdMS_TO_TICKS(50));
  uart_flush(UART_NUM_0); // Get rid of dirty RX data
  esp_vfs_dev_uart_use_driver(UART_NUM_0);
  /* No line-ending conversion in either direction. Binary frames (COBS)
     need raw bytes. CR → LF for the REPL is handled at the mux/REPL level. */
  esp_vfs_dev_uart_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_LF);
  esp_vfs_dev_uart_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_LF);

  // Set up the NVS partition system
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  return FROTH_OK;
}

froth_error_t platform_emit(uint8_t byte) {
  /* Terminal expects \r\n. VFS conversion is off (binary safety for COBS),
     so we prepend \r before \n here for REPL/console output. */
  if (byte == '\n')
    fputc('\r', stdout);
  fputc(byte, stdout);
  return FROTH_OK;
}

froth_error_t platform_emit_raw(uint8_t byte) {
  fputc(byte, stdout);
  return FROTH_OK;
}

froth_error_t platform_key(uint8_t *byte) {
  int c = fgetc(stdin);
  if (c == EOF) {
    return FROTH_ERROR_IO;
  }
  /* Set interrupt flag on 0x03 but still return the byte. The caller
     decides context: the mux clears the flag in frame mode (0x03 is
     data there), direct mode and the blocking REPL consume it as an
     interrupt, and the key prim returns it to user code where the
     executor's safe-point check will fire the interrupt. */
  if (c == 0x03) {
    froth_vm.interrupted = 1;
  }
  *byte = (uint8_t)c;
  return FROTH_OK;
}

bool platform_key_ready(void) {
  fd_set rfds;                  // Set of file descriptors to watch for reading.
  struct timeval tv = {0, 0};   // 0 timeout, return immediately
  FD_ZERO(&rfds);               // Zero them
  FD_SET(fileno(stdin), &rfds); // Add stdin file descriptor to set

  int ret = select(fileno(stdin) + 1, &rfds, NULL, NULL, &tv); // do the check
  if (ret > 0) {
    return true;
  } // we have a byte
  else {
    return false;
  }
}

void platform_check_interrupt(struct froth_vm_t *vm) {
  if (platform_key_ready()) {
    int c = fgetc(stdin);
    if (c == 0x03) {
      vm->interrupted = 1;
    } else {
      ungetc(c, stdin);
    }
  }
}

_Noreturn void platform_fatal(void) {
  // esp_restart(); // Avoid this, it will result in an infinite loop.
  while (1) {
  };
}

/* Shared NVS staging buffer. Static to keep it off the task stack (2KB). */
static uint8_t nvs_staging[FROTH_SNAPSHOT_BLOCK_SIZE];

froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len) {
  nvs_handle_t handle;
  const char *key = slot == 0 ? "snap_a" : "snap_b";

  if (offset + len > FROTH_SNAPSHOT_BLOCK_SIZE) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  esp_err_t err = nvs_open("froth", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  // Read existing blob so we can merge, or start from zeroes
  size_t existing_len = FROTH_SNAPSHOT_BLOCK_SIZE;
  err = nvs_get_blob(handle, key, nvs_staging, &existing_len);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    memset(nvs_staging, 0, sizeof(nvs_staging));
    existing_len = 0;
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  // Patch in the new bytes at the requested offset
  memcpy(nvs_staging + offset, buf, len);

  // New blob size is the larger of existing data and the write extent
  size_t new_len = existing_len;
  if (offset + len > new_len) {
    new_len = offset + len;
  }

  err = nvs_set_blob(handle, key, nvs_staging, new_len);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  nvs_close(handle);
  return FROTH_OK;
}

froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len) {
  nvs_handle_t handle;
  const char *key = slot == 0 ? "snap_a" : "snap_b";

  esp_err_t err = nvs_open("froth", NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  size_t stored_len = FROTH_SNAPSHOT_BLOCK_SIZE;
  err = nvs_get_blob(handle, key, nvs_staging, &stored_len);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  if (offset + len > stored_len) {
    nvs_close(handle);
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  memcpy(buf, nvs_staging + offset, len);

  nvs_close(handle);
  return FROTH_OK;
}

froth_error_t platform_snapshot_erase(uint8_t slot) {
  nvs_handle_t handle;

  esp_err_t err = nvs_open("froth", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  const char *key = slot == 0 ? "snap_a" : "snap_b";
  err = nvs_erase_key(handle, key);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Nothing to erase, that's fine
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  nvs_close(handle);
  return FROTH_OK;
}
