# Froth Implementation Timeline

*Last reviewed: 2026-03-11*
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
- [ ] Deserializer: parse payload, rebuild heap objects, apply slot bindings
- [ ] **Proof**: def a word, serialize to buffer, wipe, restore from buffer — word still works

> **Timebox warning**: persistence is the highest-risk milestone. Priority order if time-constrained:
> 1. Correctness of format and restore logic (RAM round-trip)
> 2. File-backed storage for POSIX (save/restore survive process restart)
> 3. Full A/B atomicity with CRC and generation counters

### Mar 10 (Tue) — Strong push: persistence finish + ESP32 first contact
- [ ] File-backed save/restore on POSIX (write to `froth.snap`)
- [ ] `save`, `restore`, `wipe` words
- [ ] A/B image selection, header CRC, payload CRC
- [ ] Boot sequence: restore snapshot on startup, `autorun` under `catch`
- [ ] Boot error handling: `main.c` checks return values from `froth_ffi_register` and `froth_evaluate_input`, exits with message on failure
- [ ] Safe boot escape (CAN window during boot)
- [ ] ESP32 port: `platform_esp32.c`, `boards/esp32/`, ESP-IDF CMake integration
- [ ] **Proof**: define `autorun`, `save`, restart → it runs. `wipe` resets to base.
- [ ] **Proof**: LED blink from Froth REPL on real ESP32 hardware

> **Risk**: Tue is overloaded. If persistence takes the full day, ESP32 slides to Wed.

### Mar 11 (Wed) — Evaluator refactor + quotation introspection + region
- [ ] Evaluator refactor: split `froth_evaluator.c` into `froth_toplevel.c` + `froth_builder.c` (see `docs/concepts/evaluator-refactor.md`)
- [ ] ESP32 port (if slipped from Tue)
- [ ] `q.len`, `q@` (quotation introspection — enables richer `see`)
- [ ] `q.pack` (build quotation from stack values)
- [ ] `mark` / `release` (FROTH-Region — heap watermark, keeps workshop experimentation tidy)
- [ ] `arity!` (stack-effect metadata for slots — supports tooling + web editor)
- [ ] `info` shows overlay heap usage (user code bytes vs total)
- [ ] `see` shows stack effect for primitives (pull from `froth_ffi_entry_t`)
- [ ] Strict bare identifiers ADR: design only — decide whether identifier execution should error on undefined slots instead of creating them (forward-reference strategy needed for quotations)
- [ ] **Proof**: `[ 1 2 + ] q.len` → `[3]`; `mark ... release` reclaims heap

---

## Phase 2: Workshop Preparation (Mar 12–18)

> Phase 2 leans heavily on AI-assisted porting and frontend work.
> The kernel is feature-complete after Phase 1. Phase 2 is ecosystem.

### Mar 12–13 (Thu–Fri) — ESP32 dual-core architecture + audio FFI
- [ ] Dual-core architecture ADR (Froth VM on Core 1, audio engine on Core 0)
- [ ] FreeRTOS task setup, UART routing, shared parameter struct
- [ ] AI-assisted: port ESP32Forth I2S/DAC/ADC/GPIO/timer bindings to Froth FFI
- [ ] Audio engine skeleton in C (Core 0): oscillator, DAC output
- [ ] Froth FFI bridge to audio parameters (`osc.freq`, `osc.wave`, etc.)
- [ ] **Proof**: set oscillator frequency from Froth REPL, hear audio output

### Mar 14–15 (Sat–Sun) — Web editor + flash tooling + Link Mode
- [ ] Web editor: WebSerial connection to ESP32
- [ ] Froth syntax highlighting, library/board definition loading
- [ ] Flash tooling integration (esptool.py wrapper or equivalent)
- [ ] Link Mode (STX/ETX framing, `#ACK`/`#NAK`) — enables reliable editor→device communication
- [ ] Machine-readable `#STACK`/`#ERR` protocol lines for editor integration
- [ ] **Proof**: edit `.froth` in web editor, push to device, it updates

### Mar 16–17 (Mon–Tue) — ESP32 persistence + workshop hardening
- [ ] ESP32 NVS/flash backend for snapshot persistence
- [ ] Save/restore survives power cycle on real hardware
- [ ] Safe boot escape (GPIO pin or CAN window)
- [ ] End-to-end test: flash → REPL → define synth → save → power cycle → autorun
- [ ] Workshop failure mode testing (bad input, heap exhaustion, bricked recovery)

### Mar 18 (Wed) — Workshop prep
- [ ] Example synth patches in Froth (oscillators, filters, LFOs, envelopes)
- [ ] Participant documentation / cheat sheet
- [ ] Pre-flash ESP32 boards
- [ ] Dry run of workshop flow

## Kernel "Definition of Done"

- [x] No GC
- [x] No implicit allocation during primitive execution
- [x] Coherent redefinition works
- [x] Errors recover to prompt
- [x] `perm` passes tests

## Workshop "Definition of Done"

- [ ] LED blink from Froth REPL on ESP32
- [ ] Interrupt stops runaway loop
- [ ] `save` survives power cycle on ESP32
- [ ] `wipe` returns to base-only state
- [ ] `"Hello" s.emit` works
- [ ] Hex literals work (`0xFF`)
- [ ] Synth audio controllable from Froth REPL
- [ ] Web editor can push code to device
- [ ] 15 pre-flashed ESP32 boards ready

## Deferred (post-workshop)

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
| FROTH-Region | Post-break | Mar 11 | Pulled forward — workshop heap hygiene. |
| q.len/q@/q.pack | Post-break | Mar 11 | Pulled forward — enables richer `see`, metaprogramming. |
