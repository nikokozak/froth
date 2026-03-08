# Froth Implementation Progress

*Last updated: 2026-03-08*

## Current Status

**Phase**: String-Lite landed. Multi-line input, `see`, `info` next.
**Blocking issues**: ~2 days behind original schedule but Ctrl-C landed early, buying buffer.
**Morale check**: `"Hello" s.emit` → `Hello` — strings are real.

## What's Done

- Build system: CMake with compile-time configurable cell width, stack capacities, heap size, slot table size, line buffer size
- `froth_types.h`: cell typedefs, format macros, error enum with stable explicit numbering (ADR-016), 3-bit LSB value tagging (7 tags assigned, 1 reserved), `froth_make_cell`, `froth_wrap_payload` (ADR-011), `FROTH_TRUE`/`FROTH_FALSE`, `FROTH_TRY` error propagation macro, `FROTH_CELL_*` macro naming convention, `FROTH_MAX_CELL_VALUE`/`FROTH_MIN_CELL_VALUE` payload range macros, `froth_native_word_t` typedef for C functions implementing Froth words
- `froth_vm.h` / `froth_vm.c`: VM struct bundling DS, RS, CS, heap, `thrown` field, `last_error_slot` field. Static backing arrays, global `froth_vm` instance.
- `froth_stack.h` / `froth_stack.c`: reusable stack struct, push/pop/peek/depth with bounds checking (no longer owns global instances — moved to VM)
- `platform.h` / `platform_posix.c`: console I/O (`platform_emit`, `platform_key`, `platform_key_ready`) using fputc/fgetc/poll
- `froth_heap.h` / `froth_heap.c`: single linear heap (uint8_t backing), `froth_heap_allocate_bytes`, `froth_heap_allocate_cells` (returns `froth_cell_t*` directly), `froth_heap_cell_ptr` accessor (no longer owns global instance — moved to VM)
- `froth_slot_table.h` / `froth_slot_table.c`: flat array with linear scan, find/create/get/set for impl and prim, name lookup by index, name storage in heap. Null/0 guards on get_impl/get_prim. Prim field now uses `froth_native_word_t`.
- `froth_reader.h` / `froth_reader.c`: tokenizer — numbers (decimal, hex `0x`, binary `0b`; ADR-021), identifiers, tick-identifiers, brackets, `p[` pattern opener, string literals `"..."` with escape sequences (ADR-023), line comments (`\`), nested paren comments (`( ... )`), EOF. `froth_reader_peek` for lookahead.
- `froth_evaluator.h` / `froth_evaluator.c`: evaluator — number pushing, identifier resolution + immediate execution via executor, tick-identifier handling (FROTH_SLOT), two-pass contiguous quotation building (ADR-010), two-pass pattern building with validation (ADR-013). `resolve_or_create_slot` helper. `count_quote_body` handles nested `p[...]` depth. `count_and_typecheck_pattern_body` validates letters (single a-z), number range (0-255), and `FROTH_MAX_PERM_SIZE` cap.
- `froth_executor.h` / `froth_executor.c`: executor — walks quotation bodies dispatching on cell tags. `froth_execute_slot` (prim-first, then impl), `froth_execute_quote` (iterate body cells).
- `froth_primitives.h` / `froth_primitives.c`: primitive table now uses `froth_ffi_entry_t` with stack effects and help text on all 32 entries. Grouped by category (core, arithmetic, comparison, bitwise, I/O, pattern, string, control flow, error handling, display). Core: `def`, `get`, `call`. Arithmetic: `+`, `-`, `*`, `/mod` (wrapping via unsigned cast, ADR-011). Comparisons: `<`, `>`, `=` (returning -1/0). Bitwise: `and`, `or`, `xor`, `invert`, `lshift`, `rshift` (logical shifts with payload masking). I/O: `emit` (low byte), `key`, `key?`. Pattern: `pat` (quotation → PatternRef, validates indices), `perm` (stack rewrite using PatternRef + window size, fixed-size scratch buffer, index-flipped for 0=TOS convention). String: `s.emit`, `s.len`, `s@`, `s.=` (ADR-023). Control flow: `choose`, `while`. Error handling: `catch`, `throw` (ADR-015). Division-by-zero error. Type checking on all ops. `FROTH_MAX_PERM_SIZE` (default 8) caps both pattern length and window size.
- `froth_ffi.h` / `froth_ffi.c`: FFI public API (ADR-019). Four functions: `froth_pop` (number, type-checked), `froth_pop_tagged` (any cell, decomposed), `froth_push` (number), `froth_throw` (set thrown + return sentinel). `froth_ffi_entry_t` struct with name, word, stack_effect, help. Convenience macros: `FROTH_FFI` (function def + metadata struct), `FROTH_POP`/`FROTH_PUSH` (stack sugar), `FROTH_BIND` (table entry reference). `froth_ffi_register` walks null-terminated table, creates slots, sets prims. Error code ranges: kernel 1–299, FFI 300+.
- `froth_repl.h` / `froth_repl.c`: interactive REPL loop — read line, evaluate, print stack. Rich cell display. Error display with numeric code, name, and faulting word (`error(2): stack underflow in "perm"`). DS/RS snapshot and restore on error. Blank-line skipping, clean EOF exit. "Prompt never dies."
- Build-time stdlib embedding: CMake `file(READ HEX)` pipeline (ADR-014). `cmake/embed_froth.cmake` script generates null-terminated `const char[]` headers from `.froth` source files. No external tool dependencies.
- `src/lib/core.froth`: shuffle words (`dup`, `swap`, `drop`, `over`, `rot`, `-rot`, `nip`, `tuck`) via `perm`. Control flow: `if`. Combinators: `set`, `dip`, `keep`, `bi`, `times`. Arithmetic: `negate`, `abs`. I/O: `cr`. All defined in Froth with `: ;` sugar, `\` doc comments, HTDP-style documentation (purpose, stack effect, example), inline `( )` stack effects.
- `froth_evaluate_input` signature changed to `const char*`.
- Error enum reorganized: stable explicit values (ADR-016). Runtime errors (1–13), reader errors (100+), internal sentinel `FROTH_ERROR_THROW = -1`. Old fine-grained slot/internal errors collapsed to user-meaningful codes (`FROTH_ERROR_UNDEFINED_WORD`, `FROTH_ERROR_TYPE_MISMATCH`, etc.).
- `def` accepts any value, not just callables (ADR-017, spec v1.2). Slots double as zero-cost mutable storage. `set` defined as `swap def` in stdlib.
- Spec conformance fixes: `/mod` result order, `not` renamed to `invert`
- Stack display: inline quotation/pattern expansion (`[1 2 +]`, `p[a b]`, nested quotes), falls back to `<q:N>` above 8 tokens. Slots show `<s:name>`. Strings show `"contents"` with escape formatting (`\n`, `\t`, `\r`, `\\`, `\"`, `\xHH` for non-printables).
- Error context: `last_error_slot` on VM, executor sets it before dispatch, REPL displays faulting word name
- `froth_fmt.h` / `froth_fmt.c`: shared formatting helpers (`emit_string`, `format_number`) used by primitives and REPL
- `.` primitive (pop and print), `.s` primitive (print stack non-destructively), `words` primitive (list all slot names)
- `: ;` sugar: `: foo 1 + ;` desugars to `'foo [ 1 + ] def`. Reader treats `;` as close-bracket (ADR-018). Evaluator special-cases `:` identifier.
- REPL thinned: stack display now delegates to `.s` primitive; error display uses shared `emit_string`/`format_number`
- `main.c` uses `froth_ffi_register` for boot — same path for kernel primitives and board FFI tables
- Board package structure: `boards/<board>/` separation from kernel `src/`. `FROTH_BOARD_BEGIN`/`FROTH_BOARD_END`/`FROTH_BOARD_DECLARE` macros for binding tables.
- `boards/posix/ffi_posix.c` / `ffi_posix.h`: POSIX board package. `gpio.mode` and `gpio.write` print trace output; `ms` uses `usleep`. Demonstrates full `FROTH_FFI` macro usage.
- LED blink demo proof: `: blink 1 gpio.mode [ dup 1 gpio.write 500 ms 0 gpio.write 500 ms ] while ;` runs, shows alternating HIGH/LOW trace output with delays
- Ctrl-C / interrupt flag (ADR-020): `SIGINT` handler sets `volatile int interrupted` on VM, checked at safe points (executor loop, `while` iterations). Triggers `FROTH_ERROR_PROGRAM_INTERRUPTED` (code 14) via throw. REPL recovers via existing catch. `platform_init()` added to platform layer.
- Hex/binary number literals (ADR-021): `0xFF`, `0b1010`, negative forms (`-0x1A`). Parsed in `try_parse_number`, emits same `TOKEN_NUMBER` — no evaluator changes. Payload range constrained by 3-bit tag (CELL_BITS - 3 usable bits).
- REPL backspace handling: `0x7F` (DEL) and `0x08` (BS) erase last character with `\b space \b` terminal sequence. Guards against underflow at position 0.
- `>r`, `r>`, `r@` return stack primitives. RS quotation balance check: executor snapshots RS depth on quotation entry, asserts match on exit. Violation throws `FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS` (code 15) with cleared error context (structural error, not attributable to a single word). ADR-022.
- Nested paren comment support: `( ( a -- a a ) )` now works. Comment skipper tracks depth.
- Evaluator error propagation fix: `froth_evaluate_input` now returns reader errors instead of silently stopping.
- Spec fix: `times` reference definition corrected (`>r` stashes `q`, not `swap >r`).
- String-Lite (ADR-023): `FROTH_TOKEN_BSTRING` in reader, escape sequences (`\"`, `\\`, `\n`, `\t`, `\r`), unknown escapes error. Evaluator allocates on heap (length cell via `memcpy` + bytes + null terminator). Executor pushes `FROTH_BSTRING` tag. `pop_bstring` helper extracts length and data pointer. `FROTH_BSTRING_LEN_MAX` (128) caps token-level string size.
- ADRs: 001–014 (prior), 015 (catch/throw via C-return propagation), 016 (stable explicit error codes), 017 (def accepts any value), 018 (colon-semicolon sugar), 019 (FFI public C API), 020 (interrupt flag via signal handler), 021 (hex/binary literals), 022 (RS quotation balance check), 023 (String-Lite heap layout)

## In Progress

Nothing in progress.

## Blocked / Waiting

Nothing blocked.

## Next Up

1. Multi-line input (bracket/string depth tracking, `..` continuation prompt)
2. `see`, `info`

## Open Questions

- CS holds `froth_cell_t` currently and is unused — will it need a different entry type for the eventual trampoline refactor? (deferred to FROTH-Perf)
- `divmod`: INT_MIN / -1 is UB in C (result overflows). Need to decide: wrap or error? (deferred — edge case, not blocking)
- Tick syntax: spec grammar says `'name` (prefix, no space) but spec examples use `' name` (space-separated). Reader currently requires prefix form. Need ADR to pick one. (deferred — not blocking)
- `while` stack discipline: too strict for REPL exploration? Revisit after `>r`/`r>`/`r@` and `times` exist — the pressure may ease naturally. (deferred)
- ~~String-Lite timing~~: moved to next-up, targeting Mar 8.
- ~~POSIX GPIO story~~: resolved — print trace output.
