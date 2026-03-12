#include "ffi.h"
#include "froth_boot.h"

int main(void) {
  froth_boot(froth_board_bindings);
  return 0;
}
