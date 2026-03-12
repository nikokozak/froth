#include "froth_vm.h"
#include "platform.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void interrupt_handler(int signum) {
  if (signum != SIGINT) {
    return;
  }
  froth_vm.interrupted = 1;
  return;
}

froth_error_t platform_init(void) {
  if (signal(SIGINT, interrupt_handler) == SIG_ERR) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

froth_error_t platform_emit(uint8_t byte) {
  if (fputc(byte, stdout) == EOF) {
    return FROTH_ERROR_IO;
  }
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
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  return poll(&pfd, 1, 0) > 0;
}

#ifdef FROTH_HAS_SNAPSHOTS
static const char *snap_path(uint8_t slot) {
  return slot == 0 ? FROTH_SNAPSHOT_PATH_A : FROTH_SNAPSHOT_PATH_B;
}

froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len) {
  const char *file = snap_path(slot);
  FILE *file_ptr;
  file_ptr = fopen(file, "rb");

  if (file_ptr == NULL) {
    return FROTH_ERROR_IO;
  }

  if (fseek(file_ptr, offset, SEEK_SET)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }
  if (!fread(buf, len, 1, file_ptr)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }

  fclose(file_ptr);
  return FROTH_OK;
}

froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len) {
  const char *file = snap_path(slot);
  FILE *file_ptr;
  file_ptr = fopen(file, "r+b");

  if (file_ptr == NULL) {
    file_ptr = fopen(file, "w+b");
    if (file_ptr == NULL) {
      return FROTH_ERROR_IO;
    }
  }

  if (fseek(file_ptr, offset, SEEK_SET)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }
  if (!fwrite(buf, len, 1, file_ptr)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }

  fclose(file_ptr);
  return FROTH_OK;
}

froth_error_t platform_snapshot_erase(uint8_t slot) {
  const char *file = snap_path(slot);
  if (remove(file) && errno != ENOENT) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}
#endif
