#include "froth_vm.h"
#include "froth_repl.h"
#include "froth_evaluator.h"
#include "froth_primitives.h"
#include "froth_lib_core.h"
#include "platform.h"
#include "ffi_posix.h"

int main(void) {
  froth_ffi_register(&froth_vm, froth_primitives);
  froth_ffi_register(&froth_vm, froth_board_bindings);
  platform_init();
  froth_evaluate_input(froth_lib_core, &froth_vm);
  froth_repl_start(&froth_vm);
  return 0;
}
