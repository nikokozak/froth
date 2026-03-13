/* TODO: ESP-IDF platform implementation */
#include "platform.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_vfs_dev.h" /* uart_vfs_dev_use_driver */
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>

froth_error_t platform_init(void) {
  // Disable stdio buffering before installing driver.
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  // Install UART driver so fgetc(stdin) blocks properly.
  esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  esp_vfs_dev_uart_use_driver(UART_NUM_0);
  esp_vfs_dev_uart_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
  return FROTH_OK;
}

froth_error_t platform_emit(uint8_t byte) {
  fputc(byte, stdout);
  return FROTH_OK;
}

froth_error_t platform_key(uint8_t *byte) {
  int c = fgetc(stdin);
  if (c == EOF) {
    return FROTH_ERROR_IO;
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

_Noreturn void platform_fatal(void) {
  esp_restart();
  while (1) {
  };
}
