#include "froth_boot.h"
#include "froth_evaluator.h"
#include "froth_fmt.h"
#include "froth_lib_core.h"
#include "froth_primitives.h"
#include "froth_repl.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "platform.h"
#ifdef FROTH_HAS_USER_PROGRAM
#include "froth_user_program.h"
#endif
#ifdef FROTH_HAS_BOARD_LIB
#include "froth_board_lib.h"
#endif

#ifdef FROTH_HAS_LINK
#include "froth_console_mux.h"
#endif

#ifdef FROTH_HAS_SNAPSHOTS
#include "froth_snapshot.h"
#endif

static void boot_fail(const char *step, froth_error_t err) {
  emit_string("boot: ");
  emit_string(step);
  emit_string(" failed (error ");
  emit_string(format_number(err));
  emit_string(")\n");
  platform_fatal();
}

static bool poll_for_safe_boot() {
  emit_string("boot: CTRL-C for safe boot\n");
  bool safe_boot = false;
  for (int i = 0; i < 75; i++) {
    platform_delay_ms(10);
    while (platform_key_ready()) {
      uint8_t byte;
      platform_key(&byte);
      if (byte == 0x03)
        safe_boot = true;
      if (froth_vm.interrupted) {
        froth_vm.interrupted = 0;
        safe_boot = true;
      }
    }
  }
  return safe_boot;
}

void froth_boot(const froth_ffi_entry_t *board_bindings) {
  froth_error_t err;
  bool safe_boot = false; // Whether we skip restore & autorun

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

  froth_tbuf_init(&froth_vm);

  err = platform_init();
  if (err)
    boot_fail("platform init", err);

  froth_tbuf_init(&froth_vm); // Transient Scratch Buffer

  err = froth_evaluate_input(froth_lib_core, &froth_vm);
  if (err)
    boot_fail("stdlib load", err);

#ifdef FROTH_HAS_BOARD_LIB
  err = froth_evaluate_input(froth_board_lib, &froth_vm);
  if (err)
    boot_fail("froth boardlib load", err);
#endif

  froth_vm.boot_complete = 1;
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;

  safe_boot = poll_for_safe_boot();
  if (!safe_boot) {

    bool restored = false;
#ifdef FROTH_HAS_SNAPSHOTS
    /* Attempt restore from snapshot storage. Failure is not fatal —
     * first boot has no snapshot, corrupt snapshots are skipped. */
    froth_error_t snap_err = froth_evaluate_input("restore", &froth_vm);
    if (snap_err != FROTH_OK) {
      froth_vm.ds.pointer = 0;
    } else {
      restored = true;
    }
#endif
#ifdef FROTH_HAS_USER_PROGRAM
    if (!restored) {
      err = froth_evaluate_input(froth_user_program, &froth_vm);
      if (err)
        boot_fail("user program load", err);
    }
#endif

    froth_evaluate_input("[ 'autorun call ] catch drop", &froth_vm);
  } else {
    emit_string("boot: Safe Boot, skipped restore and autorun.");
  }

#ifdef FROTH_HAS_LINK
  froth_console_mux_start(&froth_vm);
#else
  froth_repl_start(&froth_vm);
#endif
}
