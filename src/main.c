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
  froth_vm.boot_complete = 1; // Set boot flag, overlays will now be applied to new definitions.
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer; // Set heap watermark after core library is loaded, so that we can report heap usage of user code separately.
  froth_repl_start(&froth_vm);
  return 0;
}
