# Froth Implementation Progress

*Last updated: 2026-03-19*

## Current Status

**Phase**: Thesis push (Phase 3). Workshop Mar 24. Thesis deadline Apr 20. Immediate focus: user programs, HAL bindings (LEDC/PWM, I2C), string bridge. Then WiFi, library system, RP2040 port.
**Blocking issues**: `screen` unusable on macOS (PTY error, not a Froth issue).
**Known limitations**: Console notifications remain lossy under sustained output, but the daemon now emits an explicit dropped-output notice instead of failing silently. Async eval model deferred (eval is still blocking RPC; long-running programs work but no start/accept acknowledgment).

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
- `froth_repl_start`: blocking REPL loop restored in `froth_repl.c`. Wraps `froth_repl_init` + `platform_key` + `froth_repl_accept_byte` + `froth_repl_evaluate`. Used when `FROTH_HAS_LINK` is not defined. Verified with save/restore cycle.
- `froth_boot.c`: `FROTH_HAS_LINK` gate on console mux vs blocking REPL. `FROTH_HAS_SNAPSHOTS` gate on snapshot include and restore. Targets without link get `froth_repl_start`, targets with link get `froth_console_mux_start`.
- ESP32 NVS snapshot backend: `nvs_flash_init` in `platform_init` (with erase-and-retry on `NO_FREE_PAGES`/`NEW_VERSION_FOUND`). `platform_snapshot_write` reads existing blob, patches at offset, writes back. `platform_snapshot_read` reads whole blob, copies requested slice. `platform_snapshot_erase` tolerates `NOT_FOUND`. Namespace `"froth"`, keys `"snap_a"`/`"snap_b"`. `nvs_flash` added to ESP-IDF `REQUIRES`. `FROTH_HAS_SNAPSHOTS`, `FROTH_SNAPSHOT_BLOCK_SIZE`, `FROTH_BOARD_NAME` added to ESP-IDF compile definitions.
- ESP32 stack overflow fix: snapshot workspace (name table, object table, walk stack, ram_buffer, header, reader tables) moved from stack to static BSS `froth_snapshot_workspace_t`. NVS staging buffer (2KB) also static. Fixes `LoadProhibited` crash on `save` and boot-loop crash on `restore`. Total static cost: ~4.8KB on `FROTH_HAS_SNAPSHOTS` targets.
- ESP32 persistence proven on hardware: define word, save, power cycle, restore. A/B rotation, wipe, multiple saves all verified.
- `dangerous-reset` primitive (ADR-037): clears overlay slots, restores heap to watermark, zeroes DS/RS/CS, clears thrown/last_error_slot/call_depth/mark. Returns `FROTH_ERROR_RESET` (code 20) which the REPL catches as a clean top-level abort (prints "reset", no error display, no stale snapshot restore). Available on all targets (not gated behind `FROTH_HAS_SNAPSHOTS`). Named `dangerous-reset` to prevent accidental invocation.
- `wipe` revised (ADR-037): now calls `froth_prim_dangerous_reset` internally instead of duplicating overlay-clear logic. Erases both snapshot slots, then resets.
- Strict bare identifiers (ADR-041): top-level bare identifier resolution no longer creates slots.
- `RESET_REQ`/`RESET_RES` protocol messages (ADR-037): message types 0x09/0x0A. Device-side `handle_reset` in `froth_link.c` calls `froth_prim_dangerous_reset`, maps `FROTH_ERROR_RESET` sentinel to status 0, returns INFO-shaped payload (heap, slots, version). Go CLI: `froth reset` command (serial + daemon paths), `session.Reset()`, daemon `deviceReset()` RPC. REPL buffer intentionally untouched (link and REPL own separate input streams). End-to-end proven via socat PTY pair.
- CS trampoline executor (ADR-040): `froth_execute_quote` rewritten as iterative trampoline loop. CALL tags in quotation bodies resolved inline (push CS frame for callee, zero C stack cost). `froth_cs_frame_t` holds `{quote_offset, ip}`, purpose-built `froth_cs_t` replaces generic `froth_stack_t` for CS. Re-entrant via `cs_base` partitioning (prims like `while`, `catch`, `call` invoke it from C, each gets its own CS partition). Two depth limits: `FROTH_CS_CAPACITY` (256, total Froth nesting) and `FROTH_REENTRY_DEPTH_MAX` (64, C stack safety for prim-driven re-entries). `trampoline_depth` counter on VM tracks re-entries. `call_depth` field removed. RS balance check at trampoline exit only. `froth_evaluator_handle_identifier` uses `froth_slot_find_name` directly; undefined names error without side effects. Forward references inside quotations, tick-identifiers, and colon sugar still create slots via `resolve_or_create_slot`. Fixes ghost slot bug where typos after `reset` left dangling heap pointers that poisoned subsequent `restore`.
- ADR-042 extension UX and local target (Mar 18): extension rewritten per ADR-042. Actions moved to native VS Code surfaces (view/title, editor/title, status bar). TreeView shows metadata only. `viewsWelcome` for first-run flow (Connect Device, Try Local, Run Doctor). Lazy daemon start via `froth daemon start --background`. Extension tracks ownership, stops daemon on deactivate if it started it. Local POSIX target: daemon `--local` flag spawns `./build64/Froth` via stdin/stdout pipes with `localTransport` (same COBS protocol). Status RPC reports `target: serial|local` and `reconnecting` state. Red interrupt status bar item during running state. Send Selection and Send File in editor title bar for `.froth` files.
- ADRs: 001-014 (prior), 015 (catch/throw via C-return propagation), 016 (stable explicit error codes), 017 (def accepts any value), 018 (colon-semicolon sugar), 019 (FFI public C API), 020 (interrupt flag via signal handler), 021 (hex/binary literals), 022 (RS quotation balance check), 023 (String-Lite heap layout), 025 (multi-line input), 026 (snapshot persistence implementation), 027 (platform snapshot storage API), 028 (board and platform architecture), 029 (build targets and toolchain management), 030 (platform_check_interrupt + safe boot), 031 (hardening: error codes + guards), 032 (mark/release heap watermark), 033 (link transport v1: COBS binary framing), 034 (console multiplexer architecture), 035 (host daemon architecture), 036 (protocol sideband probes), 037 (host-centric deployment with overlay user program), 039 (host tooling UX and daemon lifecycle), 040 (CS trampoline executor), 041 (strict bare identifiers), 042 (extension UX, daemon lifecycle, local target)
- VS Code extension design: `docs/concepts/vscode-extension-design.md`. Two modes (live/project), form-level sync, subscription probes at safe points, thin TypeScript over daemon. Workshop skeleton: connect, send selection, console, status bar.
- Host tooling roadmap: `docs/concepts/host-tooling-roadmap.md`. Phased plan with status tracking for AI agents.
- Target tier model: 32-bit (full Froth), 16-bit (tethered), 8-bit (tethered). Documented in `docs/concepts/target-tiers-and-tethered-mode.md`.
- Tooling architecture proposal reviewed. Key decisions: binary payloads (not JSON), COBS framing (replaces STX/ETX), device-first principle preserved. See `docs/concepts/tooling-and-link-architecture-proposal-2026-03.md`.
- Console multiplexer (ADR-034): poll-and-dispatch main loop replaces `froth_repl_start`. REPL restructured from blocking loop to `froth_repl_accept_byte` + `froth_repl_evaluate`. Mux routes bytes: 0x00 → frame accumulation, direct bytes → REPL. Gated behind `FROTH_HAS_LINK`.
- `froth_transport.h` / `froth_transport.c`: COBS encode/decode (in-place decode safe), frame header parse/build with CRC32 validation, `froth_link_send_frame` helper, inbound frame accumulation buffer. Link error codes 250-256.
- `froth_link.h` / `froth_link.c`: protocol dispatcher. HELLO_RES (cell_bits, heap, slots, version, board), EVAL_RES (evaluate source, return status/error/fault word), INFO_RES (heap, overlay, slots, version). ERROR response for unknown types. `payload_writer_t` helper for building response payloads.
- `froth_crc32_update`: incremental CRC32 for non-contiguous data (link header + payload separated by CRC field).
- `FROTH_BOARD_NAME` CMake compile definition for device identification in HELLO_RES.
- End-to-end proof: COBS-framed HELLO, EVAL, INFO piped through stdin on POSIX build, structured responses decoded and verified (5/5 CRC pass).
- EVAL_RES `stack_repr` field now populated: `format_stack` in `froth_link.c` formats DS as `[1 2 3]` with tag-aware display (numbers decimal, slots named, others tagged).
- Host CLI (`tools/cli/`): Go, `go.bug.st/serial` for serial I/O, hand-rolled arg parser. Packages: `internal/protocol/` (COBS, frame, messages), `internal/serial/` (port, discover), `internal/session/` (connect, HELLO, EVAL), `internal/daemon/` (daemon server, RPC, client), `cmd/` (info, send, doctor, build, flash, daemon). Auto-discovery probes USB-serial ports with HELLO_REQ.
- End-to-end proof: Go CLI ↔ POSIX Froth binary via socat PTY pair. `info` prints device metadata, `send "1 2 +"` returns `[3]`, `send "1 drop drop"` returns `error(2) in "perm"`. Device stack persists across EVAL requests (expected).
- CLI commands: `doctor` (Go, cmake, make, serial ports, ESP-IDF, device probe), `build` (POSIX cmake+make or ESP-IDF idf.py, project root auto-detection), `flash` (ESP-IDF with port detection, POSIX prints binary path).
- Daemon skeleton (ADR-035): Unix domain socket at `~/.froth/daemon.sock`, JSON-RPC 2.0, serial connection ownership, reconnect on device loss, event broadcast (console, connected, disconnected, reconnecting). `froth daemon start|stop|status`. CLI auto-detects daemon, `--serial` forces bypass, `--daemon` forces routing. Concurrency reviewed: atomic reconnect guard, sync.Once for shutdown, broadcast outside lock, nil-map guard on client disconnect.
- ESP-IDF link layer enabled: `FROTH_HAS_LINK=1` in ESP-IDF CMakeLists, transport/link/mux sources added. Full host CLI ↔ ESP32 protocol proven on real hardware: info, eval, reset, strings, redefine-after-reset.
- ESP32 binary-safe UART: VFS line-ending conversion disabled (both RX and TX set to `ESP_LINE_ENDINGS_LF` = no conversion). CR → LF and CRLF coalescing handled at the mux/REPL level in direct mode only. Frame mode gets raw bytes. Fixes COBS frame corruption from 0x0D → 0x0A translation.
- ESP32 `platform_key` 0x03 fix: sets interrupt flag as side effect but returns the byte normally. Mux clears the false interrupt in frame mode (0x03 is COBS data there). Direct mode and blocking REPL consume 0x03 as interrupt. `key` prim returns the byte, executor safe-point check fires interrupt. All three line endings (CR, LF, CRLF) tested and correct.
- VS Code extension v0.0.2 (`tools/vscode/`): sidebar with device info (board, heap, slots, cell bits), action buttons (Interrupt, Reset, Save, Wipe), live heap/slot metrics via `info()` RPC (refreshed after each eval/reset). `sendFile` = reset + eval whole file (ADR-037). `Cmd+Shift+Enter` for Send File. `daemon-client.ts` exposes `reset()`, `interrupt()`, `info()`. Console output via OutputChannel.
- `platform_emit_raw`: new platform API function for COBS frame output (no line-ending conversion). `platform_emit` on ESP32 prepends `\r` before `\n` for terminal output. Fixes REPL staircase caused by disabling VFS TX conversion.
- Daemon rewrite (Mar 18): removed `frameCh` channel (dropped frames), removed `rpcTimeout` (eval can run forever). Per-request registered waiter (`registerWaiter`/`waitResponse`): serial reader delivers decoded frames directly to the waiting goroutine, no buffering, no drain, no race. `writeMu` serializes all serial writes (frames and raw interrupt). Disconnect signals blocked waiters via `disconnectCh`. Safety timeout (10s) for info/reset; no timeout for eval. Non-blocking notification channels (capacity 64) for console broadcast.
- Chunked eval: auto-splits source >253 bytes on top-level form boundaries (depth-aware, tracks `[ ] : ;` nesting). Same logic in daemon and direct session paths.
- Daemon `interrupt` RPC: writes raw 0x03 to serial via `writeMu` (not `reqMu`). Works during in-progress eval. VS Code extension sends interrupt via fresh daemon connection (bypasses per-socket sequential RPC handling).
- Extension running state: `idle/running/disconnected/no-daemon`. During `running`, all framed commands except interrupt are rejected. Status bar shows "Running" with spin icon. Sidebar shows only interrupt button. `sendFile` sets running before reset (prevents queueing race). State transitions guarded by `deriveIdleState()` (prevents clobbering by stale notifications).
- `key` primitive Ctrl-C: throws `ERR.INTERRUPT` directly on 0x03 (ESP32) or on SIGINT-interrupted `fgetc` (POSIX). No stale interrupt flag, no byte pushed to stack. Consistent error code 14 on both platforms.
- Resilient HELLO probe: retry loop with COBS error recovery, `ResetInputBuffer` between attempts, 5s deadline. Handles DTR-triggered boot contamination on macOS. First-after-flash command now succeeds reliably. Discovery filtered to `/dev/cu.*` only (avoids double DTR resets from `tty.*` aliases).
- Session path updated: `CommandTimeout` (10s) for reset/info, no timeout for eval. `errors.Is` for timeout detection.
- ESP32 `platform_emit`: suppresses NUL (0x00) to prevent COBS frame delimiter corruption on multiplexed serial line.
- Spec v0.6: Interactive Development spec updated from STX/ETX text framing to COBS binary framing. Interrupt semantics clarified for multiplexed console. Host tooling section updated. References ADR-033, ADR-034.
- `froth reset` CLI command proven on real ESP32 hardware.
- Host-tooling hardening tranche (Mar 19): shared `serial.Transport` helpers now back both direct CLI sessions and daemon backends; lexical chunking moved into `internal/session` with tests for comments, strings, patterns, and oversized-form rejection; daemon `status` now reports pid + daemon/api versions; extension supervision moved into a dedicated module with strict API-version enforcement, owned-PID shutdown, explicit local runtime path support, and deterministic wrong-mode restart; daemon startup now probes for stale sockets, writes the pid file only after bind, accepts `status` before device handshake completes, and background start waits for ready before printing the pid. Local POSIX mode now uses explicit runtime selection and unbuffered non-TTY stdio, which fixes the stdin/stdout transport path.

## In Progress

- ADR-038 streaming snapshot serializer: accepted. Current static BSS workspace is a band-aid (~2.8KB + 2KB NVS staging). Streaming v2 format targets ~344B writer, ~280B reader.

## Blocked / Waiting

- `screen` unusable on macOS: "Sorry Could not find a PTY" error. Not a Froth issue, but limits serial monitor options.

## Next Up

### Workshop (Mar 20–23, workshop Mar 24)
1. User programs: `FROTH_USER_PROGRAM` CMake embed + cold-boot eval (ADR-037)
2. LEDC/PWM bindings: `ledc.setup`, `ledc.duty`, `ledc.freq`
3. I2C bindings: `i2c.init`, `i2c.write-byte`, `i2c.read-byte`
4. Fix 4-table FFI metadata limit (silent truncation in `froth_ffi.c`)
5. String bridge ADR + `froth_pop_bstring` / `froth_push_bstring` public API
6. Flash 15 boards, test workshop flow

### Post-workshop thesis push (Mar 27 — Apr 20)
7. WiFi bindings (uses string bridge): `wifi.connect`, `wifi.status`, `wifi.ip`
8. HTTP client or server (phone-controllable demo)
9. Library/include system: ADR + host-side include resolution
10. VS Code syntax highlighting (TextMate grammar for `.froth`)
11. `catch` truth convention ADR (resolve before public release)
12. RP2040 platform port (proves multi-target portability)
13. One ported library (stepper, servo, or sensor driver)
14. Thesis demo project
15. Getting started guide
16. Shared host request engine unification deferred: daemon and direct CLI still share transport, chunking, and probe code, but request execution is not fully collapsed into one host core yet. Defer until after manual editor validation so the refactor starts from a known-good baseline.

## Open Questions

- ~~CS holds `froth_cell_t` currently and is unused~~: resolved — CS now uses `froth_cs_frame_t` (two-cell struct) with purpose-built `froth_cs_t` stack type. Trampoline executor (ADR-040).
- `divmod`: INT_MIN / -1 is UB in C (result overflows). Need to decide: wrap or error? (deferred — make wrapping normative in spec, see TIMELINE deferred/near-term)
- Tick syntax: spec grammar says `'name` (prefix, no space) but spec examples use `' name` (space-separated). Reader currently requires prefix form. Need ADR to pick one. (deferred — not blocking)
- `while` stack discipline: too strict for REPL exploration? Revisit after `>r`/`r>`/`r@` and `times` exist — the pressure may ease naturally. (deferred)
- ~~Strict bare identifiers~~: resolved — ADR-041. Top-level bare identifiers no longer create slots.
- ~~Boot error handling~~: resolved — `boot_fail()` + `platform_fatal()` in main.c.
- ~~`wipe` does not restore redefined base words~~: resolved — primitive redefinition is now forbidden (ADR-031). `def` rejects slots with existing prims. Stdlib words defined via `: ;` can still be redefined (they're impl-only, no prim).
- ~~String-Lite timing~~: moved to next-up, targeting Mar 8.
- ~~POSIX GPIO story~~: resolved — print trace output.
- ~~`\0` escape inconsistency~~: resolved — removed from spec. String-Lite stays `\0`-free; FROTH-String will handle binary buffers.
- ~~Spec editorial typos~~: resolved — version headers, cross-references fixed.
