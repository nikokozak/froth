#include "platform.h"

#include <stdio.h>
#include <unistd.h>
#include <poll.h>

froth_error_t platform_emit(uint8_t byte) {
  if (fputc(byte, stdout) == EOF) { return FROTH_ERROR_IO; }
  return FROTH_OK;
}

froth_error_t platform_key(uint8_t* byte) {
  int c = fgetc(stdin);
  if (c == EOF) { return FROTH_ERROR_IO; }
  *byte = (uint8_t)c;
  return FROTH_OK;
}

bool platform_key_ready(void) {
  struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
  return poll(&pfd, 1, 0) > 0;
}
