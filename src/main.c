#include "froth_vm.h"
#include "froth_repl.h"
#include "froth_evaluator.h"
#include "froth_primitives.h"
#include "froth_snapshot.h"
#include "froth_fmt.h"
#include "froth_lib_core.h"
#include "platform.h"
#include "ffi_posix.h"

int main(void) {
  froth_ffi_register(&froth_vm, froth_primitives);
  froth_ffi_register(&froth_vm, froth_board_bindings);
#ifdef FROTH_HAS_SNAPSHOTS
  froth_ffi_register(&froth_vm, froth_snapshot_prims);
#endif
  platform_init();
  froth_evaluate_input(froth_lib_core, &froth_vm);
  froth_vm.boot_complete = 1;
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;

#ifdef FROTH_HAS_SNAPSHOTS
  /* Attempt restore from snapshot storage. Failure is not fatal —
   * first boot has no snapshot, corrupt snapshots are skipped. */
  froth_error_t snap_err = froth_evaluate_input("restore", &froth_vm);
  if (snap_err != FROTH_OK) {
    froth_vm.ds.pointer = 0;
  }
#endif

  froth_repl_start(&froth_vm);
  return 0;
}
