# Froth Implementation Timeline

*Last reviewed: 2026-03-04*
*Source: Froth Implementation Roadmap v0.3 (Feb 25 → End of Spring Break)*

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

### Mar 5 (Thu) — catch/throw + "prompt never dies" (originally Mar 4)
- [ ] `catch` installs handler frame (DS depth snapshot minimum)
- [ ] `throw` unwinds to nearest `catch`, restores DS depth
- [ ] REPL wraps each top-level evaluation in implicit `catch`
- [ ] **Proof**: deliberate errors return to prompt with stack restored

### Mar 5–Mar 6 (Thu–Fri) — FFI Stage 1 + LED blink demo
- [ ] `froth_pop_cell` / `froth_push_cell` / `froth_throw`
- [ ] `FROTH_FN`, `FROTH_TRY`, `FROTH_PRIM`
- [ ] Bind: `gpio.mode`, `gpio.write`, `ms`
- [ ] Static registration table
- [ ] **Proof**: `: blink ( pin -- ) ... ;` runs from REPL, blinks LED

### Mar 7 (Sat) — Ctrl-C / interrupt flag
- [ ] CAN (0x18) sets VM interrupt flag
- [ ] VM checks flag at safe points; throws ERR.INTERRUPT
- [ ] **Proof**: infinite loops can be stopped without reset

### Mar 8–Mar 9 (Sun–Mon) — Introspection essentials
- [ ] `.s`, `words`, `see` (token dump is fine)
- [ ] `.` (print integer, no heap alloc)
- [ ] `info` banner: version, heap free, snapshot status
- [ ] **Proof**: inspect definitions and recover from mistakes quickly

### Mar 10–Mar 12 (Tue–Thu) — Snapshot overlay persistence
- [ ] A/B snapshot region, header + CRC, generation selection
- [ ] Serialize/restore overlay (QUOTE-only) via name table + object IDs
- [ ] `save`, `restore`, `wipe`
- [ ] Boot restores snapshot, runs `autorun` under `catch`
- [ ] Safe boot escape (pin or CAN window)
- [ ] **Proof**: define `autorun`, `save`, power cycle → it runs. `wipe` resets to base.

> **Timebox warning**: flash persistence is the highest-risk milestone. Priority order if time-constrained:
> 1. Correctness of format and restore logic (round-trip in RAM)
> 2. Minimal flash implementation for demo (single-image + backup)
> 3. Full A/B atomicity and wear hardening

### Mar 13–Mar 15 (Fri–Sun) — Link Mode + host tool (OPTIONAL)
- [ ] FROTH-LINK handshake, STX/ETX framing
- [ ] `#ACK`/`#NAK` textual replies
- [ ] Minimal `froth-link` tool: send file line-by-line or frame-by-frame
- [ ] **Proof**: edit `.froth` file, push to device, it updates; Direct Mode still works

## Kernel "Definition of Done"

- [ ] No GC
- [ ] No implicit allocation during primitive execution
- [ ] Coherent redefinition works
- [ ] Errors recover to prompt
- [ ] `perm` passes tests

## Demo "Definition of Done"

- [ ] LED blink from bare terminal
- [ ] Interrupt stops runaway loop
- [ ] `save` survives power loss
- [ ] `wipe` returns to base-only state

## Deferred (post–Spring Break)

- DTC/native promotion (FROTH-Perf)
- Named frames compiler pass (FROTH-Named)
- Checked kinds/contracts as selectable build profile (FROTH-Checked)
- FROTH-Region (mark/release)
- FROTH-String-Lite
- Richer `see` (pretty printing, source retention policies)

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
