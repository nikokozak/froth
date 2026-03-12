# Froth Implementation Progress

*Last updated: 2026-03-11*

## Current Status

**Phase**: Snapshot persistence Stage 2 complete. File-backed `save`/`restore`/`wipe` working. Boot-time restore working. `autorun` under `catch` next.
**Blocking issues**: ~1.5 days behind original schedule. `autorun`, boot hardening, ESP32 port remain for this week.
**Morale check**: define words, save, kill process, restart — they come back. 17/17 smoke tests pass.

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
- `froth_primitives.h` / `froth_primitives.c`: primitive table now uses `froth_ffi_entry_t` with stack effects and help text on all 34 entries. Grouped by category (core, arithmetic, comparison, bitwise, I/O, pattern, string, control flow, error handling, display/introspection). Core: `def`, `get`, `call`. Arithmetic: `+`, `-`, `*`, `/mod` (wrapping via unsigned cast, ADR-011). Comparisons: `<`, `>`, `=` (returning -1/0). Bitwise: `and`, `or`, `xor`, `invert`, `lshift`, `rshift` (logical shifts with payload masking). I/O: `emit` (low byte), `key`, `key?`. Pattern: `pat` (quotation → PatternRef, validates indices), `perm` (stack rewrite using PatternRef + window size, fixed-size scratch buffer, index-flipped for 0=TOS convention). String: `s.emit`, `s.len`, `s@`, `s.=` (ADR-023). Control flow: `choose`, `while`. Error handling: `catch`, `throw` (ADR-015). Division-by-zero error. Type checking on all ops. `FROTH_MAX_PERM_SIZE` (default 8) caps both pattern length and window size.
- `froth_ffi.h` / `froth_ffi.c`: FFI public API (ADR-019). Four functions: `froth_pop` (number, type-checked), `froth_pop_tagged` (any cell, decomposed), `froth_push` (number), `froth_throw` (set thrown + return sentinel). `froth_ffi_entry_t` struct with name, word, stack_effect, help. Convenience macros: `FROTH_FFI` (function def + metadata struct), `FROTH_POP`/`FROTH_PUSH` (stack sugar), `FROTH_BIND` (table entry reference). `froth_ffi_register` walks null-terminated table, creates slots, sets prims. Error code ranges: kernel 1–299, FFI 300+.
- `froth_repl.h` / `froth_repl.c`: interactive REPL loop — read line, evaluate, print stack. Rich cell display. Error display with numeric code, name, and faulting word (`error(2): stack underflow in "perm"`). DS/RS snapshot and restore on error. Blank-line skipping, clean EOF exit. Multi-line input: lightweight depth scanner tracks bracket nesting (including `:` sugar), paren comments, and unclosed strings; `..` continuation prompt; `\n` line separator preserves line-comment correctness.
- Build-time stdlib embedding: CMake `file(READ HEX)` pipeline (ADR-014). `cmake/embed_froth.cmake` script generates null-terminated `const char[]` headers from `.froth` source files. No external tool dependencies.
- `src/lib/core.froth`: shuffle words (`dup`, `swap`, `drop`, `over`, `rot`, `-rot`, `nip`, `tuck`) via `perm`. Control flow: `if`. Combinators: `set`, `dip`, `keep`, `bi`, `times`. Arithmetic: `negate`, `abs`. I/O: `cr`. All defined in Froth with `: ;` sugar, `\` doc comments, HTDP-style documentation (purpose, stack effect, example), inline `( )` stack effects.
- `froth_evaluate_input` signature changed to `const char*`.
- Error enum reorganized: stable explicit values (ADR-016). Runtime errors (1–13), reader errors (100+), internal sentinel `FROTH_ERROR_THROW = -1`. Old fine-grained slot/internal errors collapsed to user-meaningful codes (`FROTH_ERROR_UNDEFINED_WORD`, `FROTH_ERROR_TYPE_MISMATCH`, etc.).
- `def` accepts any value, not just callables (ADR-017, spec v1.2). Slots double as zero-cost mutable storage. `set` defined as `swap def` in stdlib.
- Spec conformance fixes: `/mod` result order, `not` renamed to `invert`
- Stack display: inline quotation/pattern expansion (`[1 2 +]`, `p[a b]`, nested quotes), falls back to `<q:N>` above 8 tokens. Slots show `<s:name>`. Strings show `"contents"` with escape formatting (`\n`, `\t`, `\r`, `\\`, `\"`, `\xHH` for non-printables).
- Error context: `last_error_slot` on VM, executor sets it before dispatch, REPL displays faulting word name
- `froth_fmt.h` / `froth_fmt.c`: shared formatting helpers (`emit_string`, `format_number`) used by primitives and REPL
- `.` primitive (pop and print), `.s` primitive (print stack non-destructively), `words` primitive (list all slot names), `see` (quotation body dump or `<primitive>`), `info` (version, heap usage, slot count)
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
- `froth_snapshot.h` / `froth_snapshot_writer.c` / `froth_snapshot_reader.c`: snapshot serializer + deserializer (Stage 1). Writer: two-pass architecture (pass 1 discovers names + objects via depth-first postorder traversal; pass 2 emits LE binary payload). Reader: single-pass (read names → create slots, load objects → allocate directly into heap, apply bindings → set impl + overlay flag). Explicit traversal stack (no recursion). Heap offset → object ID dedup. All value types persistable (NUMBER, QUOTE, SLOT, PATTERN, BSTRING, CONTRACT). Overflow checks on all emit/read calls. Reserved fields (contract_obj_id, meta_flags, meta_len) for forward compatibility. `FROTH_SNAPSHOT_MAX_NAME_LEN` (63) caps snapshot name entries.
- Snapshot error codes in `froth_types.h`: OVERFLOW (200), FORMAT (201), UNRESOLVED (202), BAD_CRC (203), INCOMPAT (204), NO_SNAPSHOT (205), BAD_NAME (206). Each code has exactly one meaning.
- Spec updated: snapshot token tags and obj_kind reuse `froth_tag_t` values directly (NUMBER=0 through CALL=6)
- RAM round-trip proof: 7/7 smoketest (quote, number, cross-word call, nested quote, string, pattern, base word survival). 261 bytes payload.
- `froth_crc32.h` / `froth_crc32.c`: IEEE 802.3 CRC32, bitwise (no lookup table). Verified against canonical test vector.
- Platform snapshot storage API (ADR-027): offset-based `platform_snapshot_read`/`write`/`erase`. Board opt-in via `FROTH_HAS_SNAPSHOTS` CMake define with linker enforcement. Capacity check: kernel-owned, compile-time `FROTH_SNAPSHOT_BLOCK_SIZE` with optional runtime override.
- `platform_posix.c`: POSIX implementation of snapshot storage. Per-call `fopen`/`fclose` (no held handles). A/B files via `FROTH_SNAPSHOT_PATH_A`/`_B` CMake vars.
- `froth_snapshot.c`: header build/parse, ABI hash (CRC32 over cell_bits + endian + format version), A/B slot selection (pick active for restore, pick inactive for save, generation counter).
- `froth_snapshot_prims.c`: `save`, `restore`, `wipe` primitives. Guarded by `#ifdef FROTH_HAS_SNAPSHOTS`. Registered via `froth_ffi_register` in `main.c`.
- `froth_slot_table.c`: `froth_slot_reset_overlay` (renamed from `reset_pointer_to_overlay_watermark`) now clears name, impl, prim, and overlay flag on all overlay slots.
- Boot-time restore: `main.c` attempts `restore` after `boot_complete`. Failure is non-fatal (first boot, corrupt snapshots silently skipped).
- 17/17 file-backed persistence smoke tests pass: all value types, A/B rotation, multiple saves, wipe, corrupt file rejection, cross-referencing words, mutable state, recursion.
- ADRs: 001–014 (prior), 015 (catch/throw via C-return propagation), 016 (stable explicit error codes), 017 (def accepts any value), 018 (colon-semicolon sugar), 019 (FFI public C API), 020 (interrupt flag via signal handler), 021 (hex/binary literals), 022 (RS quotation balance check), 023 (String-Lite heap layout), 025 (multi-line input), 026 (snapshot persistence implementation), 027 (platform snapshot storage API)

## In Progress

Nothing in progress.

## Blocked / Waiting

Nothing blocked.

## Next Up

1. `autorun` under `catch`: if slot `autorun` is bound after restore, execute `[ 'autorun call ] catch`. Errors reported, never prevent REPL entry.
2. Boot error handling: `main.c` checks return values from `froth_ffi_register` and `froth_evaluate_input`, exits with message on failure.
3. ESP32 port: `platform_esp32.c`, `boards/esp32/`, ESP-IDF CMake integration
4. Evaluator refactor: split into `froth_toplevel.c` + `froth_builder.c` (see `docs/concepts/evaluator-refactor.md`)

## Open Questions

- CS holds `froth_cell_t` currently and is unused — will it need a different entry type for the eventual trampoline refactor? (deferred to FROTH-Perf)
- `divmod`: INT_MIN / -1 is UB in C (result overflows). Need to decide: wrap or error? (deferred — make wrapping normative in spec, see TIMELINE deferred/near-term)
- Tick syntax: spec grammar says `'name` (prefix, no space) but spec examples use `' name` (space-separated). Reader currently requires prefix form. Need ADR to pick one. (deferred — not blocking)
- `while` stack discipline: too strict for REPL exploration? Revisit after `>r`/`r>`/`r@` and `times` exist — the pressure may ease naturally. (deferred)
- Strict bare identifiers: typos create slots permanently. ADR needed before persistence ships to users. Design scheduled Mar 11, implementation deferred. (audit finding)
- Boot error handling: `main.c` ignores return values from registration/stdlib. Scheduled Mar 10 alongside boot sequence hardening. (audit finding)
- `wipe` does not restore redefined base words: `froth_slot_reset_overlay` clears overlay slots entirely — if a user redefined a base word (e.g., `: + 42 ;`), it's gone until reboot. Fix: `wipe` should trigger reboot (embedded) or full re-init (POSIX). For now, user must power cycle after `wipe`. Needs ADR before shipping to users. (audit finding)
- ~~String-Lite timing~~: moved to next-up, targeting Mar 8.
- ~~POSIX GPIO story~~: resolved — print trace output.
- ~~`\0` escape inconsistency~~: resolved — removed from spec. String-Lite stays `\0`-free; FROTH-String will handle binary buffers.
- ~~Spec editorial typos~~: resolved — version headers, cross-references fixed.
