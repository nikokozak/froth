/* TODO: ESP-IDF entry point */
#include "ffi.h"
#include "froth_boot.h"

void app_main(void) { froth_boot(froth_board_bindings); }
