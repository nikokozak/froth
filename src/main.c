#include "froth_vm.h"
#include "froth_repl.h"
#include "froth_evaluator.h"
#include "froth_primitives.h"
#include "froth_snapshot.h"
#include "froth_fmt.h"
#include "froth_lib_core.h"
#include "platform.h"
#include "ffi_posix.h"

static int test_count = 0;
static int pass_count = 0;

static froth_cell_t pop_number(froth_vm_t *vm) {
  froth_cell_t value;
  froth_pop(vm, &value);
  return value;
}

static void check_number(froth_vm_t *vm, const char *label,
                          const char *expr, froth_cell_t expected) {
  test_count++;
  froth_error_t err = froth_evaluate_input(expr, vm);
  if (err != FROTH_OK) {
    emit_string("  FAIL ");
    emit_string(label);
    emit_string(": error ");
    emit_string(format_number(err));
    emit_string("\n");
    vm->ds.pointer = 0;
    return;
  }

  froth_cell_t got = pop_number(vm);
  vm->ds.pointer = 0;

  if (got == expected) {
    emit_string("  ok   ");
    emit_string(label);
    emit_string("\n");
    pass_count++;
  } else {
    emit_string("  FAIL ");
    emit_string(label);
    emit_string(": expected ");
    emit_string(format_number(expected));
    emit_string(", got ");
    emit_string(format_number(got));
    emit_string("\n");
  }
}

static void check_eval(froth_vm_t *vm, const char *label, const char *expr) {
  test_count++;
  froth_error_t err = froth_evaluate_input(expr, vm);
  vm->ds.pointer = 0;
  if (err != FROTH_OK) {
    emit_string("  FAIL ");
    emit_string(label);
    emit_string(": error ");
    emit_string(format_number(err));
    emit_string("\n");
    return;
  }
  emit_string("  ok   ");
  emit_string(label);
  emit_string("\n");
  pass_count++;
}

static void snapshot_round_trip_test(froth_vm_t *vm) {
  static uint8_t snap_buf[FROTH_SNAPSHOT_MAX_BYTES];
  froth_snapshot_buffer_t snapshot = {.data = snap_buf, .position = 0};
  froth_error_t err;

  emit_string("--- snapshot round-trip test ---\n");

  // Define overlay words covering all value types
  froth_evaluate_input("'triple [ 3 * ] def", vm);
  froth_evaluate_input("'answer 42 def", vm);
  froth_evaluate_input("'sixfold [ triple 2 * ] def", vm);
  froth_evaluate_input("'make-adder [ [ + ] ] def", vm);
  froth_evaluate_input("'greeting \"hello\" def", vm);
  froth_evaluate_input("'my-swap p[ a b ] def", vm);
  vm->ds.pointer = 0;

  // Save
  err = froth_snapshot_save(vm, &snapshot);
  if (err != FROTH_OK) {
    emit_string("FAIL: save error ");
    emit_string(format_number(err));
    emit_string("\n");
    return;
  }
  emit_string("saved ");
  emit_string(format_number(snapshot.position));
  emit_string(" bytes\n");

  // Load (wipes overlay, restores from buffer)
  err = froth_snapshot_load(vm, &snapshot);
  if (err != FROTH_OK) {
    emit_string("FAIL: load error ");
    emit_string(format_number(err));
    emit_string("\n");
    return;
  }
  emit_string("loaded ok\n\n");

  // Verify everything survived
  check_number(vm, "quote: 9 triple", "9 triple", 27);
  check_number(vm, "number: answer", "answer", 42);
  check_number(vm, "call: 5 sixfold", "5 sixfold", 30);
  check_number(vm, "nested: 3 4 make-adder call", "3 4 make-adder call", 7);
  check_number(vm, "string: greeting s.len", "greeting s.len", 5);
  check_number(vm, "pattern: 1 2 2 my-swap perm", "1 2 2 my-swap perm drop", 2);
  check_eval(vm, "base words survive: 10 dup +", "10 dup +");

  emit_string("\n");
  emit_string(format_number(pass_count));
  emit_string("/");
  emit_string(format_number(test_count));
  emit_string(" passed\n");
}

int main(void) {
  froth_ffi_register(&froth_vm, froth_primitives);
  froth_ffi_register(&froth_vm, froth_board_bindings);
  platform_init();
  froth_evaluate_input(froth_lib_core, &froth_vm);
  froth_vm.boot_complete = 1;
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;

  snapshot_round_trip_test(&froth_vm);

  froth_repl_start(&froth_vm);
  return 0;
}
