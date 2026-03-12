#include "froth_boot.h"
#include "froth_evaluator.h"
#include "froth_fmt.h"
#include "froth_lib_core.h"
#include "froth_primitives.h"
#include "froth_repl.h"
#include "froth_snapshot.h"
#include "froth_vm.h"
#include "platform.h"

static void boot_fail(const char *step, froth_error_t err) {
  emit_string("boot: ");
  emit_string(step);
  emit_string(" failed (error ");
  emit_string(format_number(err));
  emit_string(")\n");
  platform_fatal();
}

void froth_boot(const froth_ffi_entry_t *board_bindings) {
  froth_error_t err;

  err = froth_ffi_register(&froth_vm, froth_primitives);
  if (err)
    boot_fail("register primitives", err);

  err = froth_ffi_register(&froth_vm, board_bindings);
  if (err)
    boot_fail("register board", err);

#ifdef FROTH_HAS_SNAPSHOTS
  err = froth_ffi_register(&froth_vm, froth_snapshot_prims);
  if (err)
    boot_fail("register snapshot prims", err);
#endif

  err = platform_init();
  if (err)
    boot_fail("platform init", err);

  err = froth_evaluate_input(froth_lib_core, &froth_vm);
  if (err)
    boot_fail("stdlib load", err);

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

  froth_evaluate_input("[ 'autorun call ] catch drop", &froth_vm);

  froth_repl_start(&froth_vm);
}
