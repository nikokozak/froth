# Froth Implementation Progress

*Last updated: 2026-03-03*

## Current Status

**Phase**: PatternRef encoding landed. `p[...]` reader + evaluator complete. Next: `perm` primitive, `pat` primitive, executor dispatch, stdlib shuffles.
**Blocking issues**: ~3 days behind original schedule. perm/pat milestone originally Mar 2, now Mar 2–3.
**Morale check**: Pattern literals parse, validate, and allocate correctly. Byte-packed heap layout working.

## What's Done

- Build system: CMake with compile-time configurable cell width, stack capacities, heap size, slot table size, line buffer size
- `froth_types.h`: cell typedefs, format macros, error enum, 3-bit LSB value tagging (7 tags assigned, 1 reserved), `froth_make_cell`, `froth_wrap_payload` (ADR-011), `FROTH_TRUE`/`FROTH_FALSE`, `FROTH_TRY` error propagation macro, `FROTH_CELL_*` macro naming convention
- `froth_vm.h` / `froth_vm.c`: VM struct bundling DS, RS, CS, heap. Static backing arrays, global `froth_vm` instance.
- `froth_stack.h` / `froth_stack.c`: reusable stack struct, push/pop/peek/depth with bounds checking (no longer owns global instances — moved to VM)
- `platform.h` / `platform_posix.c`: console I/O (`platform_emit`, `platform_key`, `platform_key_ready`) using fputc/fgetc/poll
- `froth_heap.h` / `froth_heap.c`: single linear heap (uint8_t backing), `froth_heap_allocate_bytes`, `froth_heap_allocate_cells` (returns `froth_cell_t*` directly), `froth_heap_cell_ptr` accessor (no longer owns global instance — moved to VM)
- `froth_slot_table.h` / `froth_slot_table.c`: flat array with linear scan, find/create/get/set for impl and prim, name lookup by index, name storage in heap. Null/0 guards on get_impl/get_prim.
- `froth_reader.h` / `froth_reader.c`: tokenizer — numbers, identifiers, tick-identifiers, brackets, `p[` pattern opener, line comments, EOF. `froth_reader_peek` for lookahead.
- `froth_evaluator.h` / `froth_evaluator.c`: evaluator — number pushing, identifier resolution + immediate execution via executor, tick-identifier handling (FROTH_SLOT), two-pass contiguous quotation building (ADR-010), two-pass pattern building with validation (ADR-013). `resolve_or_create_slot` helper. `count_quote_body` handles nested `p[...]` depth. `count_and_typecheck_pattern_body` validates letters (single a-z), number range (0-255), and `FROTH_MAX_PERM_PATTERN_SIZE` cap.
- `froth_executor.h` / `froth_executor.c`: executor — walks quotation bodies dispatching on cell tags. `froth_execute_slot` (prim-first, then impl), `froth_execute_quote` (iterate body cells).
- `froth_primitives.h` / `froth_primitives.c`: primitive registration table. Core: `def`, `get`, `call`. Arithmetic: `+`, `-`, `*`, `/mod` (wrapping via unsigned cast, ADR-011). Comparisons: `<`, `>`, `=` (returning -1/0). Bitwise: `and`, `or`, `xor`, `not`, `lshift`, `rshift` (logical shifts with payload masking). I/O: `emit` (low byte), `key`, `key?`. Division-by-zero error. Type checking on all ops.
- `froth_repl.h` / `froth_repl.c`: interactive REPL loop — read line, evaluate, print stack. Rich cell display (`Q:16`, `S:foo`, `C:bar`), named error messages (including type mismatch, undefined word, division by zero), blank-line skipping, clean EOF exit, non-fatal error recovery.
- Naming overhaul: tag-0 renamed from "Cell" to "Number" (ADR-005), spec updated to v1.0
- ADRs: 001–010 (cell width, host-native, build system, value tagging, naming, slot table, linear heap, heap accessor, call tag, contiguous quotation layout), 011 (wrapping arithmetic), 012 (perm TOS-right reading), 013 (PatternRef byte encoding)

## In Progress

- `perm` primitive (executor dispatch for FROTH_PATTERN, stack rewriting logic)
- `pat` primitive (quotation → PatternRef conversion)
- Stdlib shuffles (`dup swap drop over rot nip tuck` as `perm`-based definitions)

## Blocked / Waiting

Nothing blocked.

## Next Up

1. `perm` + `pat` primitives, executor FROTH_PATTERN dispatch
2. choose + while
3. catch/throw + "prompt never dies" (will also fix REPL stack persistence on error)

## Open Questions

- CS holds `froth_cell_t` currently — will it need a different entry type for call frames / catch handler frames?
- `divmod`: INT_MIN / -1 is UB in C (result overflows). Need to decide: wrap or error? (deferred — edge case, not blocking)
