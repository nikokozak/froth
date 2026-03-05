# Froth Ergonomics Review

*Date: 2026-03-04*
*Status: Ideas for consideration. Nothing here is decided.*

This document captures observations from a review of the spec (v1.1), interactive spec (v0.5), and implementation as of the catch/throw milestone. Items are ranked roughly by impact on user experience.

---

## 1. Critical missing affordances (spec-required, not yet implemented)

### 1a. `>r` / `r>` / `r@` (FROTH-Base mandatory)

Without return stack words, none of the stdlib combinators (`dip`, `keep`, `bi`, `tri`, `times`) can be defined. This is the single biggest ergonomic gap — users hit a wall as soon as they need anything beyond trivial stack shuffling.

**Action:** Implement before or alongside FFI milestone.

### 1b. `.` (dot) and `.s`

The interactive spec *requires* `.` (print integer) and `.s` (print stack without modification). Currently the only way to see values is the after-line stack dump. Users need inline printing to build anything useful.

**Action:** Implement. `.` needs only a small fixed scratch buffer, no heap allocation.

### 1c. `words`

Required by the interactive spec. On a bare serial terminal, `words` is how you discover the system. Without it, users are blind.

**Action:** Implement. Walk the slot table, emit names.

### 1d. Multi-line input

The interactive spec says this is *normative*. Currently, `[` without matching `]` on the same line produces `error: unterminated quotation`. The spec requires accumulating lines with a `..` continuation prompt until the expression is complete.

**Action:** Implement line accumulation with bracket depth tracking.

### 1e. `: ;` sugar

Optional in the spec, but `'foo [1 +] def` is tedious compared to `: foo 1 + ;`. The desugar is trivial. High-impact, low-cost.

**Decision needed:** Implement now (ergonomics) or defer (forces understanding of the explicit form)?

---

## 2. Spec conformance issues

### 2a. `/mod` result order is swapped

Spec: `/mod ( a b -- rem quot )` — quotient on top.
Implementation (`froth_primitives.c:126-130`): pushes quotient first (below), remainder second (TOS).

Result: `10 3 /mod` gives `[3 1]` (quot=3 below, rem=1 on top) instead of spec-required `[1 3]` (rem=1 below, quot=3 on top).

**Action:** Swap the two push calls.

### 2b. `not` vs `invert`

Spec says `invert ( a -- x )` for bitwise complement. Implementation registers it as `not`. In Forth convention, `invert` is bitwise and `not` is logical. The current naming diverges from spec and from Forth tradition.

**Action:** Rename to `invert`.

---

## 3. Stack display improvements

### ~~3a. Show token count, not heap offset~~ DONE

### ~~3b. Show quotation/pattern contents inline~~ DONE

Quotations up to 8 tokens expand inline (`[1 2 +]`), including nested quotes. Patterns show letters (`p[a b]`). Above 8 tokens, falls back to `<q:N>`.

---

## 4. Error message quality

### ~~4a. Add context to error messages~~ DONE

Errors now show the faulting word: `error(2): stack underflow in "perm"`. Single `last_error_slot` field on the VM, set in the executor before dispatch.

---

## 5. Design tensions to resolve

### 5a. Hex/binary number literals

For embedded work, hex is essential (register addresses, bitmasks). The spec allows other bases but doesn't require them.

Options: `0xFF` prefix, `$FF` prefix (some Forths), `#FF` prefix.

**Decision needed:** Pick a syntax. Probably worth an ADR.

### 5b. `while` stack discipline — too strict for exploration?

`while` enforces condition=+1 and body=stack-neutral on every iteration. Excellent for production firmware. But during REPL exploration, accumulation patterns require `>r`/`r>` (not yet available) or restructuring the loop.

Once `>r`/`r>` and `times` exist, the pressure eases. But worth asking: should there be a looser `loop` variant for quick experiments, or is the discipline worth the friction?

**Decision needed:** Revisit after `>r`/`r>` and `times` are available.

### 5c. String-Lite timing

Every message is currently `72 emit 101 emit ...`. String-Lite (`"Hello" s.emit`) is spec'd and optional, but for a "REPL-first" language it's pretty fundamental.

**Decision needed:** Before or after FFI/LED blink milestone?

### 5d. Tick syntax: `'name` vs `' name`

PROGRESS.md notes this as an open question. `'name` (prefix, no space) is compact and unambiguous to the reader. `' name` (space-separated) is more readable and more Forth-like. The spec grammar says `'name` but spec examples use `' name`.

**Decision needed:** Pick one, ADR it.

---

## 6. Stdlib gaps (definable once `>r`/`r>` exist)

These are all defined in the spec's FROTH-Stdlib section and would be added to `core.froth`:

- `dip` — `[ swap >r call r> ] def`
- `keep` — `[ over swap call swap ] def`
- `bi` — `[ -rot keep rot call ] def`
- `times` — counted iteration (uses `>r`/`r@`/`r>` + `while`)
- `negate` — `[ 0 swap - ] def`
- `abs` — `[ dup 0 < [ negate ] [ ] if ] def`
- `cr` — `[ 10 emit ] def` (recommended by interactive spec)
- `2dup`, `2drop`, `2swap` — common in practice

---

## 7. Future considerations (post-sprint)

- `q.len` / `q@` / `q.pack` — quotation introspection primitives (spec-defined, needed for `see` and metaprogramming)
- `see` — requires `q@` or equivalent to walk quotation bodies
- `info` banner — version, slot count, free heap
- `free` / `used` — heap introspection
- Machine-readable `#STACK`/`#ERR` protocol lines for tooling
- Step mode / trace mode for debugging
