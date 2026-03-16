# Froth Implementation Timeline

*Last reviewed: 2026-03-16 (CLI skeleton proven)*
*Source: Froth Implementation Roadmap v0.4 (Feb 25 → Workshop week of Mar 15)*

> Mark items as they complete. Adjust dates when they slip — don't delete the original date.
> Format: `[status] Milestone — target date (original date if slipped)`

## Guiding Principles

1. Build the smallest thing that can blink.
2. Keep the kernel tiny; move features into libraries (express in Froth when possible).
3. Make errors recoverable early — `catch`/`throw` and "prompt stays alive" are non-negotiable.
4. Defer performance tiers — start with QuoteRef interpretation; add DTC/native once correctness is proven.

## Day-by-Day Plan

### Feb 25–28 (Wed–Sat) — Repo + VM skeleton (originally Feb 25 only)
- [x] Data stack (DS) + minimal call stack (CS)
- [x] Slot table with stable identity (intern by name)
- [x] Linear heap allocator for QuoteRef/PatternRef
- [x] Minimal console I/O (UART read/write)
- [x] **Proof**: hardcode `[ 1 2 ]`, execute it, verify stack

### Mar 1 (Sat) — Reader + tokenization (originally Feb 26)
- [x] Tokenizer: numbers, identifiers, `'name`, `[ ... ]`, `p[ ... ]`
- [x] Simple interpreter loop for token streams
- [x] **Proof**: REPL reads `1 2` and prints stack `[1 2]`

### Mar 1 (Sat) — Core call/def + basic execution (originally Feb 27)
- [x] `call`, `def`, `get`
- [x] Execute QuoteRefs
- [x] Basic error codes: ERR.STACK, ERR.TYPE, ERR.UNDEF
- [x] **Proof**: `' inc [ 1 + ] def  41 inc` works (once `+` exists)

### Mar 2 (Mon) — FROTH-Base arithmetic + I/O (originally Feb 28–Mar 1)
- [x] `+ - * /mod`
- [x] Comparisons: `< > =`
- [x] Bitwise: `and or xor not lshift rshift`
- [x] `emit`, `key`, `key?`
- [x] **Proof**: interactive math works; echo loop deferred until `while` (Mar 3)

### Mar 2–4 (Mon–Wed) — perm + pat + canonical shuffles (originally Mar 2)
- [x] PatternRef encoding from `p[ ... ]` (ADR-013: byte-packed heap layout)
- [x] `pat` primitive (quotation → PatternRef)
- [x] `perm` correctly rewires top n DS items
- [x] **Proof**: perm test suite passes (dup, swap, drop, over, rot, -rot, nip, tuck — both `p[...]` and `pat` forms)
- [x] Spec fix: `-rot` pattern corrected from `p[c a b]` to `p[a c b]` (stale from TOS-left era)
- [x] Define `dup swap drop over rot -rot nip tuck` in Froth as library words (ADR-014: CMake `file(READ)` embedding)

### Mar 4 (Wed) — choose + while (originally Mar 3)
- [x] `choose` primitive
- [x] `while` primitive with stack discipline rules
- [x] Define `if := choose call` in Froth stdlib
- [x] **Proof**: `10 [ dup 5 > ] [ 1 - ] while` → `[5]`

### Mar 4 (Wed) — catch/throw + "prompt never dies" (originally Mar 4)
- [x] `catch` snapshots DS/RS/CS depths, intercepts all errors (ADR-015)
- [x] `throw` stores error code in `vm->thrown`, returns `FROTH_ERROR_THROW` sentinel
- [x] REPL snapshots/restores DS/RS on error, displays `error(N): description`
- [x] Error enum reorganized with stable explicit values (ADR-016)
- [x] **Proof**: `[ 1 drop drop ] catch` → `[2]`; `42 throw` at top level → error, prompt alive

### Mar 5–6 (Thu–Fri) — REPL essentials
- [x] `.` (print integer, no heap allocation)
- [x] `.s` (print stack without modification)
- [x] `words` (walk slot table, emit names)
- [x] `: ;` sugar (desugar to `'name [...] def`, ADR-018)
- [x] `froth_fmt` module extracted (shared `emit_string`, `format_number`)
- [x] **Proof**: `: inc 1 + ; 5 inc .` prints `6`; `words` lists all defined names

### Mar 6–Mar 7 (Fri–Sat) — FFI Stage 1 + LED blink demo (was Mar 5–6)
- [x] `froth_pop` / `froth_push` / `froth_pop_tagged` / `froth_throw` (ADR-019)
- [x] `FROTH_FFI`, `FROTH_POP`, `FROTH_PUSH`, `FROTH_BIND` convenience macros
- [x] `froth_ffi_entry_t` registration struct with name, word, stack_effect, help
- [x] `froth_ffi_register` walks null-terminated table into slot table
- [x] `froth_native_word_t` typedef replaces `froth_primitive_fn_t` throughout
- [x] Kernel primitives migrated to `froth_ffi_entry_t` with stack effects + help on all 28 entries
- [x] `main.c` boots via `froth_ffi_register` — unified path for kernel and FFI
- [x] Error code ranges: kernel 1–299, FFI 300+ (convention, not enforced)
- [x] Bind: `gpio.mode`, `gpio.write`, `ms` (POSIX stubs with trace output)
- [x] Board package structure: `boards/posix/`, `FROTH_BOARD_BEGIN`/`END`/`DECLARE` macros
- [x] **Proof**: `: blink` runs from REPL, shows alternating HIGH/LOW trace with delays

### Mar 6 (Thu) — Ctrl-C / interrupt flag (was Mar 7, originally Mar 8)
- [x] SIGINT handler sets `volatile int interrupted` on VM (ADR-020)
- [x] VM checks flag at safe points; throws ERR.INTERRUPT (code 14)
- [x] `platform_init()` added to platform layer for signal setup
- [x] **Proof**: `[ 1 ] [ ] while` + Ctrl-C → `error(14): interrupted in "while"`, prompt alive

### Mar 7 (Sat) — Light day: reader extensions + return stack
- [x] Hex/binary number literals (ADR-021 — syntax: `0xFF`, `0b1010`)
- [x] Basic backspace handling in REPL readline (`0x7F`/`0x08`)
- [x] `>r`, `r>`, `r@` primitives (RS already exists on VM)
- [x] RS quotation balance check (ADR-022): executor asserts RS depth unchanged on quotation exit
- [x] **Proof**: `0xFF` → `[255]`; `0b1010` → `[10]`; `-0x1A` → `[-26]`
- [x] **Proof**: `5 >r 10 r> +` → `[15]`; `[ 5 >r ] call` → `error(15): unbalanced return stack`

### Mar 7 (Sat) — Bonus: stdlib combinators + bugfixes
- [x] Stdlib: `dip`, `keep`, `bi`, `times`, `negate`, `abs`, `cr` in `core.froth`
- [x] `core.froth` rewritten: `: ;` sugar, `\` doc comments, HTDP-style docs, inline stack effects
- [x] Nested paren comment support in reader (depth tracking)
- [x] Evaluator error propagation fix (`froth_evaluate_input` no longer swallows reader errors)
- [x] Spec fix: `times` reference definition corrected
- [x] **Proof**: `3 [ 42 . ] times` prints `42 42 42`; `5 [ 2 * ] [ 1 + ] bi` → `[10 6]`

### Mar 8 (Sun) — Strong push: String-Lite + REPL polish
- [x] FROTH-String-Lite (ADR-023 + reader `"..."` + StringRef heap layout + escape sequences)
- [x] `s.emit`, `s.len`, `s@`, `s.=` primitives
- [x] **Proof**: `"Hello" s.emit` prints `Hello`; `"Hello" s.len` → `[5]`; `"Hello" 0 s@` → `[72]`
- [x] `see` (quotation body dump / `<primitive>` for prims)
- [x] `info` banner: version (`FROTH_VERSION` CMake define), heap usage, slot count (`froth_slot_count()`)
- [x] **Proof**: `'inc see` → `[1 +]`; `'emit see` → `<primitive>`; `info` → version, heap, slots
- [x] Multi-line input (bracket/string depth tracking, `..` continuation prompt)
- [x] **Proof**: `: double ⏎ .. dup + ⏎ .. ; ⏎ 5 double` → `[10]`; multi-line paren comments, strings, nested brackets all accumulate correctly

### Mar 9–11 (Mon–Tue) — Persistence stage 1: format + RAM round-trip (was Mar 9)
- [x] Snapshot persistence design (review spec, ADR-026 for implementation choices)
- [x] Overlay ownership tracking (per-slot flag + boot_complete gate, ADR-026 §1)
- [x] Serializer: two-pass (discovery + emission), name table + dependency-ordered objects + slot bindings
- [x] Snapshot error codes (200–203) in froth_types.h
- [x] Spec updated: token/object tags reuse froth_tag_t values
- [x] Deserializer: single-pass reader (names → objects → bindings), direct-to-heap allocation, LE byte assembly
- [x] Writer bugfix: nested quote dependency collection missed outer quote when nested quote was last token
- [x] **Proof**: 7/7 smoketest — quote, number, cross-word call, nested quote, string, pattern, base word survival

> **Timebox warning**: persistence is the highest-risk milestone. Priority order if time-constrained:
> 1. Correctness of format and restore logic (RAM round-trip)
> 2. File-backed storage for POSIX (save/restore survive process restart)
> 3. Full A/B atomicity with CRC and generation counters

### Mar 10–11 (Tue–Wed) — Persistence Stage 2: file-backed save/restore/wipe (was Mar 10)
- [x] CRC32 module: IEEE 802.3 bitwise (no lookup table), verified against canonical test vector
- [x] Platform snapshot storage API (ADR-027): offset-based read/write/erase, `FROTH_HAS_SNAPSHOTS` opt-in
- [x] POSIX implementation: per-call fopen/fclose, A/B files, erase via remove()
- [x] Snapshot header: 50-byte envelope with magic, format version, ABI hash, generation counter, CRC32 (header + payload)
- [x] A/B slot selection: pick active (restore), pick inactive (save), generation-based winner
- [x] `save`, `restore`, `wipe` primitives in `froth_snapshot_prims.c`
- [x] Boot-time restore: `main.c` attempts restore after boot_complete, failure non-fatal
- [x] Snapshot error codes cleaned up: 200–206, each with exactly one meaning
- [x] `froth_slot_reset_overlay` properly clears all overlay slot fields
- [x] **Proof**: 17/17 smoke tests — all value types, A/B rotation, wipe, corrupt file rejection, cross-refs, mutable state, recursion
- [x] `autorun` under `catch`: `[ 'autorun call ] catch drop` — silent on fresh boot, errors swallowed, clean stack
- [x] Boot error handling: `boot_fail()` prints step + error code, `platform_fatal()` halts. All init calls checked.
- [ ] Safe boot escape (CAN window during boot)
- [x] ESP32 port: `platforms/esp-idf/platform.c`, `boards/esp32-devkit-v1/`, `targets/esp-idf/`, `tools/setup-esp-idf.sh` (ADR-029)
- [x] POSIX raw terminal mode: `tcgetattr`/`tcsetattr` disables ECHO+ICANON, `sigaction` for SIGINT, REPL owns echo/backspace/Ctrl-D
- [x] `froth_boot.c`: shared boot sequence extracted from `main.c`
- [ ] **Proof**: define `autorun`, `save`, restart → it runs. `wipe` resets to base.
- [x] `platform_check_interrupt()`: platform-agnostic polling at executor safe points (ADR-030 pending). ESP-IDF polls UART, POSIX no-op.
- [x] **Proof**: LED blink from Froth REPL on real ESP32 hardware — `[ -1 ] [ 2 1 gpio.write 500 ms 2 0 gpio.write 500 ms ] while`, Ctrl-C interrupts cleanly

### Mar 11 (Wed) — Evaluator refactor + quotation introspection + region
- [ ] Evaluator refactor: split `froth_evaluator.c` into `froth_toplevel.c` + `froth_builder.c` (see `docs/concepts/evaluator-refactor.md`)
- [ ] ESP32 port (if slipped from Tue)
- [x] `q.len`, `q@` (quotation introspection — enables richer `see`) — landed Mar 14
- [ ] `q.pack` (build quotation from stack values)
- [x] `mark` / `release` (FROTH-Region — heap watermark, ADR-032) — landed Mar 14
- [ ] `arity!` (stack-effect metadata for slots — supports tooling + web editor)
- [x] `info` shows overlay heap usage (user code bytes vs total) — landed Mar 14
- [x] `see` shows stack effect for primitives (pull from `froth_ffi_entry_t`) — landed Mar 14, walks all registered FFI tables
- [ ] Strict bare identifiers ADR: design only — decide whether identifier execution should error on undefined slots instead of creating them (forward-reference strategy needed for quotations)
- [x] **Proof**: `[ 1 2 + ] q.len` → `[3]`; `mark ... release` reclaims heap

---

## Phase 2: Workshop Preparation (Mar 12–18)

> Phase 2 leans heavily on AI-assisted porting and frontend work.
> The kernel is feature-complete after Phase 1. Phase 2 is ecosystem.

### Mar 13 (Thu) — Hardening day (started early, originally Mar 14)
- [x] ADR-030: `platform_check_interrupt` + safe boot window design
- [x] ADR-031: hardening error codes and guards
- [x] ESP32 flash death spiral: diagnosed (DTR reset + UART RX noise), mitigated (flush, settle, halt-not-restart)
- [x] Safe boot escape: 750ms Ctrl-C window, skips restore + autorun
- [x] Smoke tests: edge cases, bad input, heap exhaustion, deep nesting, recursion
- [x] Call depth guard: prevents segfault on infinite recursion (catchable error 18)
- [x] Primitive redefinition forbidden (error 17), colon-sugar duplicate slot bug fixed
- [x] Bare `]` error, reader error propagation in quotation builder, slot table full error
- [x] Spec updated: CAN → ETX for interrupt byte, boot sequence with safe boot step
- [x] `platform_delay_ms` added to platform API
- [x] **Proof**: comprehensive smoke test battery, all findings fixed, persistence still works
- [x] `q.len`, `q@` (quotation introspection) — CALL→SLOT tag conversion on extract
- [x] `mark` / `release` (FROTH-Region, ADR-032) — single-level heap watermark, error 19 on release without mark
- [x] `see` shows stack effects (FFI metadata lookup across all registered tables), `info` shows overlay heap usage

### Mar 15 (Sat) — Ecosystem planning + ADR-033 + ADR-034
- [x] Target tier model documented (32-bit full, 16-bit tethered, 8-bit tethered)
- [x] Tooling architecture proposal reviewed (ChatGPT doc)
- [x] ADR-033: FROTH-LINK/1 binary transport (COBS framing, replaces STX/ETX)
- [x] ADR-033 reviewed, 8 issues found and fixed (CRC scope, field widths, COBS semantics, etc.)
- [x] ADR-034: console multiplexer architecture (poll-and-dispatch, REPL refactor, `key` blocking behavior)

### Mar 15 (Sat) — Console mux + device-side link layer (C)
- [x] REPL refactor: split `froth_repl_start` into `froth_repl_accept_byte` + `froth_repl_evaluate` (ADR-034)
- [x] Console mux main loop (`froth_console_mux.c`): poll-and-dispatch, byte classification, frame accumulation (ADR-034)
- [x] COBS codec (`froth_transport.c`): encode, decode, in-place decode, CRC32 incremental update
- [x] Link dispatcher (`froth_link.c`): HELLO, EVAL, INFO handlers, ERROR for unknown types
- [x] Gate behind `FROTH_HAS_LINK` CMake flag
- [x] File rename: `froth_link` → `froth_transport` (dumb pipe), `froth_link_dispatch` → `froth_link` (smart protocol)
- [x] **Proof**: 17/17 REPL smoke tests pass through the mux
- [x] **Proof**: 5/5 COBS frame round-trips (HELLO, EVAL success, EVAL error, INFO, unknown type), all CRCs verified

### Mar 16 (Sun) — Host CLI skeleton (was Mar 18–19)
- [x] Host language decision: Go, `go.bug.st/serial`
- [x] Serial port open, HELLO handshake, print device info
- [x] EVAL round-trip: send source, print structured result
- [ ] INSPECT round-trip: query word, print metadata (device-side not implemented)
- [x] EVAL_RES stack_repr populated (format_stack in froth_link.c)
- [x] **Proof**: end-to-end protocol proven (Go CLI ↔ POSIX Froth via socat PTY pair)

### Mar 19–21 (Wed–Fri) — AI-assisted host buildout
- [ ] CLI commands: doctor, build, flash, send, info
- [ ] Daemon skeleton (serial session ownership, reconnect)
- [ ] VS Code extension skeleton (connect, send selection, console panel)
- [ ] Iterative review and testing

### Interleaved kernel work (Mar 16–21, as sessions allow)
- [ ] ESP32 dual-core architecture + audio FFI
- [ ] ESP32 NVS/flash backend for snapshot persistence
- [ ] FROTH-Addr memory access primitives (ADR-024)
- [ ] Evaluator refactor if time permits

## Kernel "Definition of Done"

- [x] No GC
- [x] No implicit allocation during primitive execution
- [x] Coherent redefinition works
- [x] Errors recover to prompt
- [x] `perm` passes tests

## Workshop "Definition of Done"

- [x] LED blink from Froth REPL on ESP32
- [x] Interrupt stops runaway loop
- [ ] `save` survives power cycle on ESP32
- [ ] `wipe` returns to base-only state
- [ ] `"Hello" s.emit` works
- [ ] Hex literals work (`0xFF`)
- [ ] Synth audio controllable from Froth REPL
- [ ] Host tooling can push code to device (CLI or VS Code via FROTH-LINK/1)
- [ ] 15 pre-flashed ESP32 boards ready

## Deferred (post-workshop)

### IMPORTANT: `catch` return value vs Froth truth convention

`catch` returns 0 on success, nonzero (error code) on failure — C/POSIX convention. But Froth's truth values are 0 = false, -1 = true. This means "success is falsy": you can't branch on a `catch` result with `if`/`choose` without inverting it. This affects any user code that wants to react to catch outcomes idiomatically. Needs an ADR to decide: do we flip `catch` to return Froth-truthy values (0 = error, -1 = success)? Return a flag + error code pair? Or accept the C convention and document it? Must also audit the spec for consistency — `key?` returns Froth-style true/false, so there's already a split if `catch` stays C-style. Resolve before shipping to users.

### Near-term: ESP32 hardware deepening

- **CALL tag decoupling** (ADR TBD): Move CALL/literal distinction out of the value-tag layer (ADR-009 rework). Frees tag 6 for NativeAddr. Prerequisite for FROTH-Addr. Independent cleanup — benefits the tag space regardless.
- **FROTH-Addr profile** (ADR-024): Native address type for full-width machine addresses. Fixed address pool, width-specific memory access (`@8`/`@16`/`@32`, `!8`/`!16`/`!32`), `addr+`, `addr.pack`. FFI API additions (`froth_push_addr`/`froth_pop_addr`). Board packages provide named address constants (`gpio.base`). Target: implement when doing direct register work on ESP32, after the workshop HAL-level FFI is proven.
- FROTH-String (`s.pack` — explicit allocation from FFI buffers, `\0` support for binary buffers)
- `/mod` overflow: make wrapping behavior normative in spec (currently implementation-defined via unsigned cast, ADR-011). Align with existing code behavior.

### Medium-term: language maturation

- DTC/native promotion (FROTH-Perf)
- Named frames compiler pass (FROTH-Named); consider a "Named Lite" path first
- Checked kinds/contracts as selectable build profile (FROTH-Checked); FFI metadata makes this more practical
- FROTH-Region-Strict (fail-fast allocation gating)
- Step mode / trace mode for debugging
- Richer `see` (pretty printing, source retention policies)
- Stack effect and help text metadata for user-defined words (currently only primitives carry this via `froth_ffi_entry_t`). Would enable `see` to show full metadata for stdlib and user words. Needs a storage decision: slot table field, heap-allocated metadata, or a separate registry.

### Completed (moved out of deferred)

- ~~Board package story~~ (landed: `boards/<board>/` structure, POSIX reference board)
- ~~FROTH-String-Lite~~ (moved to Phase 1)
- ~~Hex/binary literals~~ (moved to Phase 1)
- ~~FROTH-Region mark/release~~ (moved to Phase 1)
- ~~`q.len` / `q@` / `q.pack`~~ (moved to Phase 1)

## Slip Log

| Milestone | Original Target | New Target | Reason |
|-----------|----------------|------------|--------|
| Repo + VM skeleton | Feb 25 | Feb 25–26 | Day 1 spent on foundational ADRs (cell width, tagging, build system) + stack implementation. Slot table, heap, I/O still pending. |
| Repo + VM skeleton | Feb 25–26 | Feb 25–27 | Day 3 (Feb 27): naming overhaul (ADR-005), platform I/O, heap, slot table. Quotation proof still pending. |
| Repo + VM skeleton | Feb 25–27 | Feb 25–28 | Day 4 (Feb 28): quotation proof completed, heap accessor helper (ADR-008). |
| Reader + tokenization | Feb 26 | Mar 1 | Completed in one session: tokenizer, evaluator, quotation building, REPL. ADR-009 (call tag), allocator API rework. |
| Core call/def/get | Feb 27 | Mar 1 | Completed same day as reader. VM struct refactor, executor, primitives, two-pass quotation builder (ADR-010). |
| FROTH-Base arithmetic + I/O | Feb 28–Mar 1 | Mar 2 | Landed Mar 2. Wrapping arithmetic (ADR-011), all ops + I/O complete. |
| perm + pat + stdlib | Mar 2 | Mar 2–4 | ADR-013 (byte encoding) Mar 3, `p[...]` reader/evaluator Mar 3, `pat` + `perm` primitives Mar 4. Found and fixed spec `-rot` bug. Stdlib embedding (ADR-014) and shuffle defs landed Mar 4. |
| choose + while | Mar 3 | Mar 4 | Landed same day as stdlib. `choose`, `while`, `if` all working. |
| catch/throw | Mar 4 | Mar 5 | Pushed by choose/while slip. |
| FFI Stage 1 + LED blink | Mar 5–6 | Mar 6–7 | REPL essentials (`.`, `.s`, `words`, `: ;`) inserted before FFI. |
| Ctrl-C interrupt | Mar 8 | Mar 6 | Landed early. ADR-020, SIGINT handler, safe-point checks. |
| Return stack + combinators | Mar 8–9 | Mar 7–8 | Restructured: split across light day (return stack) and push day (stdlib + strings). |
| String-Lite | Post-break | Mar 8 | Pulled forward — critical for workshop REPL experience. |
| Hex/binary literals | Post-break | Mar 7 | Pulled forward — critical for hardware register work. |
| Snapshot persistence | Mar 10–12 | Mar 9–10 | Compressed to 2 days. RAM round-trip first, file-backed second. |
| ESP32 port | Not scheduled | Mar 10–11 | Added. Platform layer + board + ESP-IDF build. |
| Link Mode | Mar 13–15 | Mar 14–15 | Moved to Phase 2 (workshop prep), paired with web editor. |
| Persistence Stage 1 | Mar 9 | Mar 9–11 | Serializer took full session; concepts deep-dive + two-pass design + ChatGPT cleanup pass. Deserializer + RAM round-trip still pending. |
| Persistence Stage 1 | Mar 9–11 | Mar 9–11 | Deserializer + RAM round-trip completed Mar 11. Writer nested-quote bug found and fixed. |
| FROTH-Region | Post-break | Mar 11 | Pulled forward — workshop heap hygiene. |
| q.len/q@/q.pack | Post-break | Mar 11 | Pulled forward — enables richer `see`, metaprogramming. |
| Persistence Stage 2 | Mar 10 | Mar 10–11 | CRC32, platform API, header, A/B selection, prims, boot restore. 17/17 smoke tests. autorun still pending. |
| Evaluator refactor + small wins | Mar 11 | Mar 14 | Mar 12–13 spent on ESP32 REPL debugging (UART buffering, line endings, Ctrl-C, raw terminal). Hardening day inserted. |
| Hardening day | Mar 14 | Mar 13 | Started early. Death spiral, safe boot, smoke tests, 5 bug fixes, 2 ADRs. |
| Dual-core + audio | Mar 12–13 | Mar 15–16 | Pushed by REPL debugging and hardening day insertion. |
| q.len/q@ | Mar 11 | Mar 14 | Slipped with evaluator refactor block. Landed alongside mark/release. |
| mark/release | Mar 11 | Mar 14 | Single-level watermark (ADR-032). Nesting deferred to FROTH-Region-Strict. |
| Link Mode | Mar 14–15 | Mar 16–17 | Redesigned as FROTH-LINK/1 binary transport (ADR-033). STX/ETX replaced with COBS framing. |
| Web editor | Mar 14–15 | Mar 19–21 | Replaced with host CLI + VS Code extension via daemon architecture. AI-assisted buildout. |
| Dual-core + audio | Mar 15–16 | Mar 16–21 | Interleaved with link/ecosystem work. |
| ESP32 persistence | Mar 16–17 | Mar 16–21 | Interleaved with link/ecosystem work. |
| Host CLI skeleton | Mar 18–19 | Mar 16 | Landed 2 days early. Go CLI with serial discovery, HELLO, EVAL proven via socat. |
