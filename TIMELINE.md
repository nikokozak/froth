# Froth Implementation Timeline

*Last reviewed: 2026-03-06*
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
- [ ] `froth_pop_cell` / `froth_push_cell` / `froth_throw`
- [ ] `FROTH_FN`, `FROTH_TRY`, `FROTH_PRIM`
- [ ] FFI registration struct carries metadata (stack effect string, help text)
- [ ] Bind: `gpio.mode`, `gpio.write`, `ms`
- [ ] Static registration table
- [ ] **Proof**: `: blink ( pin -- ) ... ;` runs from REPL, blinks LED

### Mar 8 (Sun) — Ctrl-C / interrupt flag (was Mar 7)
- [ ] CAN (0x18) sets VM interrupt flag
- [ ] VM checks flag at safe points; throws ERR.INTERRUPT
- [ ] **Proof**: infinite loops can be stopped without reset

### Mar 9–Mar 10 (Mon–Tue) — Return stack, combinators, introspection (was Mar 8–9)
- [ ] `>r`, `r>`, `r@` primitives
- [ ] Multi-line input (bracket depth tracking, `..` continuation prompt)
- [ ] Stdlib: `dip`, `keep`, `bi`, `times`, `negate`, `abs`, `cr`
- [ ] `see` (token dump of quotation body)
- [ ] `info` banner: version, heap free, slot count
- [ ] **Proof**: `' inc see` dumps definition; `5 [ dup . 1 - ] times` prints `5 4 3 2 1`

### Mar 11–Mar 13 (Wed–Fri) — Snapshot overlay persistence (was Mar 10–12)
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

### Mar 14–Mar 15 (Sat–Sun) — Link Mode + host tool (OPTIONAL, was Mar 13–15)
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
- Named frames compiler pass (FROTH-Named); consider a "Named Lite" path first
- Checked kinds/contracts as selectable build profile (FROTH-Checked); FFI metadata makes this more practical
- FROTH-Region (mark/release)
- FROTH-String-Lite (`"Hello" s.emit` — high impact for REPL usability)
- Hex/binary number literals (syntax TBD — needs ADR: `0xFF`, `$FF`, `0b1010`, etc.)
- `q.len` / `q@` / `q.pack` — quotation introspection primitives (needed for advanced `see` and metaprogramming)
- `free` / `used` — heap introspection words
- Machine-readable `#STACK`/`#ERR` protocol lines for host tooling
- Step mode / trace mode for debugging
- Richer `see` (pretty printing, source retention policies)
- Board package story (FFI bindings grouped by target hardware)

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
| Introspection essentials | Mar 8–9 | Mar 9–10 | Expanded: added `>r`/`r>`/`r@`, multi-line input, stdlib combinators. |
