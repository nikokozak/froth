# Froth Implementation Progress

*Last updated: 2026-03-04*

## Current Status

**Phase**: `catch`/`throw` landed. Error handling complete. "Prompt never dies" working. Next: FFI Stage 1 + LED blink demo.
**Blocking issues**: ~1 day behind original schedule. `catch`/`throw` originally Mar 4, landed Mar 4 (same day as `choose`/`while`).
**Morale check**: `[ 1 drop drop ] catch` → `[2]`. Errors are catchable. REPL survives everything.

## What's Done

- Build system: CMake with compile-time configurable cell width, stack capacities, heap size, slot table size, line buffer size
- `froth_types.h`: cell typedefs, format macros, error enum with stable explicit numbering (ADR-016), 3-bit LSB value tagging (7 tags assigned, 1 reserved), `froth_make_cell`, `froth_wrap_payload` (ADR-011), `FROTH_TRUE`/`FROTH_FALSE`, `FROTH_TRY` error propagation macro, `FROTH_CELL_*` macro naming convention
- `froth_vm.h` / `froth_vm.c`: VM struct bundling DS, RS, CS, heap, `thrown` field. Static backing arrays, global `froth_vm` instance.
- `froth_stack.h` / `froth_stack.c`: reusable stack struct, push/pop/peek/depth with bounds checking (no longer owns global instances — moved to VM)
- `platform.h` / `platform_posix.c`: console I/O (`platform_emit`, `platform_key`, `platform_key_ready`) using fputc/fgetc/poll
- `froth_heap.h` / `froth_heap.c`: single linear heap (uint8_t backing), `froth_heap_allocate_bytes`, `froth_heap_allocate_cells` (returns `froth_cell_t*` directly), `froth_heap_cell_ptr` accessor (no longer owns global instance — moved to VM)
- `froth_slot_table.h` / `froth_slot_table.c`: flat array with linear scan, find/create/get/set for impl and prim, name lookup by index, name storage in heap. Null/0 guards on get_impl/get_prim.
- `froth_reader.h` / `froth_reader.c`: tokenizer — numbers, identifiers, tick-identifiers, brackets, `p[` pattern opener, line comments, EOF. `froth_reader_peek` for lookahead.
- `froth_evaluator.h` / `froth_evaluator.c`: evaluator — number pushing, identifier resolution + immediate execution via executor, tick-identifier handling (FROTH_SLOT), two-pass contiguous quotation building (ADR-010), two-pass pattern building with validation (ADR-013). `resolve_or_create_slot` helper. `count_quote_body` handles nested `p[...]` depth. `count_and_typecheck_pattern_body` validates letters (single a-z), number range (0-255), and `FROTH_MAX_PERM_SIZE` cap.
- `froth_executor.h` / `froth_executor.c`: executor — walks quotation bodies dispatching on cell tags. `froth_execute_slot` (prim-first, then impl), `froth_execute_quote` (iterate body cells).
- `froth_primitives.h` / `froth_primitives.c`: primitive registration table. Core: `def`, `get`, `call`. Arithmetic: `+`, `-`, `*`, `/mod` (wrapping via unsigned cast, ADR-011). Comparisons: `<`, `>`, `=` (returning -1/0). Bitwise: `and`, `or`, `xor`, `not`, `lshift`, `rshift` (logical shifts with payload masking). I/O: `emit` (low byte), `key`, `key?`. Pattern: `pat` (quotation → PatternRef, validates indices), `perm` (stack rewrite using PatternRef + window size, fixed-size scratch buffer, index-flipped for 0=TOS convention). Control flow: `choose`, `while`. Error handling: `catch`, `throw` (ADR-015). Division-by-zero error. Type checking on all ops. `FROTH_MAX_PERM_SIZE` (default 8) caps both pattern length and window size.
- `froth_repl.h` / `froth_repl.c`: interactive REPL loop — read line, evaluate, print stack. Rich cell display (`Q:16`, `S:foo`, `C:bar`). Error display with numeric code and name (`error(2): stack underflow`). DS/RS snapshot and restore on error. Blank-line skipping, clean EOF exit. "Prompt never dies."
- Build-time stdlib embedding: CMake `file(READ HEX)` pipeline (ADR-014). `cmake/embed_froth.cmake` script generates null-terminated `const char[]` headers from `.froth` source files. No external tool dependencies.
- `src/lib/core.froth`: shuffle words (`dup`, `swap`, `drop`, `over`, `rot`, `-rot`, `nip`, `tuck`) defined via `perm`. `if` defined as `choose call`. First words written in Froth itself.
- `froth_evaluate_input` signature changed to `const char*`.
- Error enum reorganized: stable explicit values (ADR-016). Runtime errors (1–13), reader errors (100+), internal sentinel `FROTH_ERROR_THROW = -1`. Old fine-grained slot/internal errors collapsed to user-meaningful codes (`FROTH_ERROR_UNDEFINED_WORD`, `FROTH_ERROR_TYPE_MISMATCH`, etc.).
- ADRs: 001–014 (prior), 015 (catch/throw via C-return propagation), 016 (stable explicit error codes)

## In Progress

Nothing in progress.

## Blocked / Waiting

Nothing blocked.

## Next Up

1. FFI Stage 1 + LED blink demo (`froth_pop_cell`, `froth_push_cell`, `froth_throw`, `FROTH_FN`, `gpio.mode`, `gpio.write`, `ms`)

## Open Questions

- CS holds `froth_cell_t` currently and is unused — will it need a different entry type for the eventual trampoline refactor? (deferred to FROTH-Perf)
- `divmod`: INT_MIN / -1 is UB in C (result overflows). Need to decide: wrap or error? (deferred — edge case, not blocking)
- Tick syntax: spec grammar says `'name` (prefix, no space) but spec examples use `' name` (space-separated). Reader currently requires prefix form. Need ADR to pick one. (deferred — not blocking)
