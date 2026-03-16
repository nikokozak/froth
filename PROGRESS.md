# Froth Implementation Progress

*Last updated: 2026-03-15*

## Current Status

**Phase**: Ecosystem. Device-side link layer complete. REPL refactored to mux architecture. COBS transport proven end-to-end. Next: host CLI.
**Blocking issues**: ESP32 snapshots remain. Serial terminal compatibility partially resolved (minicom works, screen has macOS PTY issues).
**Morale check**: Link pipeline working. HELLO, EVAL, INFO all proven over COBS frames piped through stdin.

## What's Done

- Build system: CMake with compile-time configurable cell width, stack capacities, heap size, slot table size, line buffer size
- `froth_types.h`: cell typedefs, format macros, error enum with stable explicit numbering (ADR-016), 3-bit LSB value tagging (7 tags assigned, 1 reserved), `froth_make_cell`, `froth_wrap_payload` (ADR-011), `FROTH_TRUE`/`FROTH_FALSE`, `FROTH_TRY` error propagation macro, `FROTH_CELL_*` macro naming convention, `FROTH_MAX_CELL_VALUE`/`FROTH_MIN_CELL_VALUE` payload range macros, `froth_native_word_t` typedef for C functions implementing Froth words
- `froth_vm.h` / `froth_vm.c`: VM struct bundling DS, RS, CS, heap, `thrown` field, `last_error_slot` field. Static backing arrays, global `froth_vm` instance.
- `froth_stack.h` / `froth_stack.c`: reusable stack struct, push/pop/peek/depth with bounds checking (no longer owns global instances — moved to VM)
- `platform.h` / `platform_posix.c`: console I/O (`platform_emit`, `platform_key`, `platform_key_ready`) using fputc/fgetc/poll. `platform_fatal()` (`_Noreturn`) for unrecoverable boot failures — POSIX: `exit(1)`.
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
- Boot error handling: `main.c` checks return values from all `froth_ffi_register` calls, `platform_init`, and `froth_evaluate_input`. Failure prints step name + error code via `emit_string`/`format_number`, then `platform_fatal()`.
- `autorun` hook: `[ 'autorun call ] catch drop` after restore. Silent on fresh boot (undefined word caught and dropped). User-defined autorun errors swallowed — stack clean on REPL entry. `autorun` slot always visible in `words` (discoverability).
- 17/17 file-backed persistence smoke tests pass: all value types, A/B rotation, multiple saves, wipe, corrupt file rejection, cross-referencing words, mutable state, recursion.
- ESP32 DevKit V1 port: `platforms/esp-idf/platform.c` (UART driver install, VFS line-ending config, unbuffered stdout), `boards/esp32-devkit-v1/ffi.c` (`gpio.mode`, `gpio.write`, `gpio.read`, `ms` via FreeRTOS `vTaskDelay`), `targets/esp-idf/` (CMake project scaffolding, sdkconfig.defaults). Builds, flashes, runs REPL on real hardware.
- `tools/setup-esp-idf.sh`: fetches ESP-IDF v5.4 to `~/.froth/sdk/esp-idf/`, installs esp32 toolchain. `--force` for reinstall.
- POSIX raw terminal mode: `tcgetattr`/`tcsetattr` disables `ECHO` and `ICANON` in `platform_init()`. `atexit` restores original settings. REPL now owns echo and backspace on all platforms.
- `sigaction` replaces `signal` for SIGINT: no `SA_RESTART`, so `fgetc` returns on interrupt (Ctrl-C escapes multiline input).
- REPL character echo: `platform_emit(byte)` on every regular character, `\n` echo on line terminator. Ctrl-D (0x04) returns `FROTH_ERROR_IO` to exit REPL cleanly.
- `froth_boot.h` / `froth_boot.c`: extracted shared boot sequence from `main.c`. Used by both POSIX `main()` and ESP-IDF `app_main()`.
- GPIO proven on ESP32 hardware: `2 1 gpio.mode` + `2 1 gpio.write` turns on LED_BUILTIN.
- LED blink loop on ESP32: `[ -1 ] [ 2 1 gpio.write 500 ms 2 0 gpio.write 500 ms ] while` — real hardware blink, interruptible with Ctrl-C.
- `platform_check_interrupt()`: platform-agnostic interrupt polling at executor safe points (ADR-030). ESP-IDF: polls UART for 0x03, uses `ungetc` for non-interrupt bytes. POSIX: no-op (SIGINT handler sets flag asynchronously). Called once per quotation body cell in `froth_execute_quote`.
- ESP-IDF Ctrl-C in `platform_key()`: byte 0x03 detected at REPL level, sets `vm->interrupted`.
- ESP-IDF UART hardening: `uart_flush` + 50ms settle after driver install, double flush. `platform_fatal` halts (spin loop) instead of `esp_restart` to prevent boot-failure reboot loops.
- Safe boot window (ADR-030): 750ms poll after stdlib load. Ctrl-C during window skips restore and autorun. Dual check (byte + interrupt flag) works on both POSIX and ESP32. User sees `boot: CTRL-C for safe boot` on every boot.
- `platform_delay_ms`: new platform API function. POSIX: `usleep`. ESP-IDF: `vTaskDelay`.
- Call depth guard (ADR-031): `call_depth` counter on VM, checked in `froth_execute_quote`. Default limit 64 (`FROTH_CALL_DEPTH_MAX`). Prevents C stack overflow on infinite recursion. Catchable error (code 18). Executor restructured from `FROTH_TRY` early-returns to `err` variable + break, ensuring `call_depth--` always executes.
- Primitive redefinition forbidden (ADR-031): `def` rejects slots with existing C prim. Error code 17.
- Colon-sugar fix (ADR-031): `: foo ...` now uses `resolve_or_create_slot` instead of `froth_slot_create`. Previously created duplicate slot entries; now correctly updates the existing slot.
- Bare `]` at top level is now an error (code 107) instead of silently producing an empty quotation.
- `count_quote_body` now propagates reader errors. Previously swallowed errors from pass 1, causing misleading "unterminated quotation" when the real error was e.g. "string too long."
- Slot table full now returns `FROTH_ERROR_SLOT_TABLE_FULL` (code 16) instead of `FROTH_ERROR_HEAP_OUT_OF_MEMORY`.
- Spec updated: interrupt byte changed from CAN (0x18) to ETX (0x03, Ctrl-C). Boot sequence updated with safe boot step.
- `q.len`, `q@` quotation introspection primitives. `q@` converts internal CALL-tagged cells to user-facing SLOT tags. Bounds-checked, consistent with `s@` conventions.
- `mark` / `release` single-level heap watermark (ADR-032). `mark` snapshots heap pointer, `release` restores it. `release` without prior `mark` throws `FROTH_ERROR_NO_MARK` (code 19). Sentinel: `(froth_cell_u_t)-1`. No nesting; composable regions deferred to FROTH-Region-Strict.
- `see` rewritten: shows name, stack effect, body or `<primitive>`, help text, origin (primitive/user-defined). FFI metadata lookup via `froth_ffi_find_entry` walks all registered tables (kernel, board, snapshot). Board words like `gpio.mode` now show full metadata.
- `info` shows overlay heap usage: `heap: 708 / 4096 bytes used (20 user)`.
- `froth_ffi_find_entry`: new lookup function in `froth_ffi.c`. Static array of up to 4 registered FFI table pointers, populated by `froth_ffi_register` during boot. Searches by function pointer.
- ADRs: 001-014 (prior), 015 (catch/throw via C-return propagation), 016 (stable explicit error codes), 017 (def accepts any value), 018 (colon-semicolon sugar), 019 (FFI public C API), 020 (interrupt flag via signal handler), 021 (hex/binary literals), 022 (RS quotation balance check), 023 (String-Lite heap layout), 025 (multi-line input), 026 (snapshot persistence implementation), 027 (platform snapshot storage API), 028 (board and platform architecture), 029 (build targets and toolchain management), 030 (platform_check_interrupt + safe boot), 031 (hardening: error codes + guards), 032 (mark/release heap watermark), 033 (link transport v1: COBS binary framing), 034 (console multiplexer architecture)
- Target tier model: 32-bit (full Froth), 16-bit (tethered), 8-bit (tethered). Documented in `docs/concepts/target-tiers-and-tethered-mode.md`.
- Tooling architecture proposal reviewed. Key decisions: binary payloads (not JSON), COBS framing (replaces STX/ETX), device-first principle preserved. See `docs/concepts/tooling-and-link-architecture-proposal-2026-03.md`.
- Console multiplexer (ADR-034): poll-and-dispatch main loop replaces `froth_repl_start`. REPL restructured from blocking loop to `froth_repl_accept_byte` + `froth_repl_evaluate`. Mux routes bytes: 0x00 → frame accumulation, direct bytes → REPL. Gated behind `FROTH_HAS_LINK`.
- `froth_transport.h` / `froth_transport.c`: COBS encode/decode (in-place decode safe), frame header parse/build with CRC32 validation, `froth_link_send_frame` helper, inbound frame accumulation buffer. Link error codes 250-256.
- `froth_link.h` / `froth_link.c`: protocol dispatcher. HELLO_RES (cell_bits, heap, slots, version, board), EVAL_RES (evaluate source, return status/error/fault word), INFO_RES (heap, overlay, slots, version). ERROR response for unknown types. `payload_writer_t` helper for building response payloads.
- `froth_crc32_update`: incremental CRC32 for non-contiguous data (link header + payload separated by CRC field).
- `FROTH_BOARD_NAME` CMake compile definition for device identification in HELLO_RES.
- End-to-end proof: COBS-framed HELLO, EVAL, INFO piped through stdin on POSIX build, structured responses decoded and verified (5/5 CRC pass).

## In Progress

(nothing — device-side link layer complete, ready for host CLI)

## Blocked / Waiting

- ~~ESP32 flash death spiral~~: diagnosed (DTR-triggered reset + UART RX contamination). Mitigated with UART flush, settle window, halt-not-restart. Clean workflow: `idf.py monitor --no-reset` + manual EN reset.
- `screen` unusable on macOS: "Sorry Could not find a PTY" error. Not a Froth issue, but limits serial monitor options.

## Next Up

1. Host CLI skeleton: serial handshake, EVAL round-trip
2. AI-assisted host buildout: CLI commands, daemon, VS Code extension
3. Interleaved kernel work: audio FFI, ESP32 persistence, FROTH-Addr
4. Evaluator refactor: split into `froth_toplevel.c` + `froth_builder.c` (if time permits)

## Open Questions

- CS holds `froth_cell_t` currently and is unused — will it need a different entry type for the eventual trampoline refactor? (deferred to FROTH-Perf)
- `divmod`: INT_MIN / -1 is UB in C (result overflows). Need to decide: wrap or error? (deferred — make wrapping normative in spec, see TIMELINE deferred/near-term)
- Tick syntax: spec grammar says `'name` (prefix, no space) but spec examples use `' name` (space-separated). Reader currently requires prefix form. Need ADR to pick one. (deferred — not blocking)
- `while` stack discipline: too strict for REPL exploration? Revisit after `>r`/`r>`/`r@` and `times` exist — the pressure may ease naturally. (deferred)
- Strict bare identifiers: typos create slots permanently. ADR needed before persistence ships to users. Design scheduled Mar 11, implementation deferred. (audit finding)
- ~~Boot error handling~~: resolved — `boot_fail()` + `platform_fatal()` in main.c.
- ~~`wipe` does not restore redefined base words~~: resolved — primitive redefinition is now forbidden (ADR-031). `def` rejects slots with existing prims. Stdlib words defined via `: ;` can still be redefined (they're impl-only, no prim).
- ~~String-Lite timing~~: moved to next-up, targeting Mar 8.
- ~~POSIX GPIO story~~: resolved — print trace output.
- ~~`\0` escape inconsistency~~: resolved — removed from spec. String-Lite stays `\0`-free; FROTH-String will handle binary buffers.
- ~~Spec editorial typos~~: resolved — version headers, cross-references fixed.
