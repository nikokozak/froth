# Froth Interactive Development Specification

**Status:** Candidate profile specification  
**Version:** 0.5 (2026-02-26)  
**Profiles defined:** `FROTH-Interactive` (Direct/Link modes), `FROTH-REPL` (conventions)  
**Scope:** Defines Froth’s on-device interaction model and optional host augmentation.  
**Non-scope:** Does not modify FROTH-Core semantics.

## Foundational principle

**The device is the computer.** A person with a serial terminal and nothing else can write, test, modify, persist, and recover Froth programs. No host toolchain is required. Host-side tools augment the experience but never replace it.

This principle serves:

- **Longevity:** a Froth device found in 2045 can be reprogrammed with whatever serial terminal exists.
- **Autonomy:** the device is not a peripheral of the laptop.
- **Pedagogy:** the feedback loop (type → run → inspect → redefine) is unmediated.

---

## Modes

Froth defines two interaction modes. The device starts in **Direct Mode**. A host may request **Link Mode** via handshake. The device MUST remain usable in Direct Mode even when Link Mode is present.

### Direct Mode (default)

Direct Mode is the standard REPL.

**Behavior (normative):**
- The device reads bytes from the primary console stream and evaluates **complete expressions**.
- After each successfully evaluated top-level expression, the device prints the stack state (REPL Stack Visualization Protocol; see Section 5).
- Each top-level evaluation MUST be wrapped in an implicit `catch`:
  - On error, the error is printed and the VM returns to the prompt with stacks restored to their pre-eval depths.
- The device MUST remain in a usable state after any error (excluding hardware faults or explicit `wipe`).

**Example (bare terminal):**
```
Froth v0.5 | 187 slots | 32KB free
> 3 4 +
[7]
> : double ( x -- y ) x 2 * ;
ok
> 10 double
[20]
```


#### Recommended policy for temporary allocations (informative)

Froth intentionally avoids garbage collection. On embedded devices, the most common accidental “slow death” is unbounded heap growth caused by dynamic object construction (e.g., `q.pack`) inside long-running loops.

Recommended policies:

- If you implement **FROTH-Region**, encourage tooling and generators to use `mark`/`release` around temporary allocations.
- If you implement **FROTH-Region-Strict**, the system will fail fast when a program attempts dynamic allocation outside an active region. This is recommended on very small targets.
- Host tools operating in Link Mode MAY wrap multi-expression sends in an explicit region scope (e.g., send `mark` first, then definitions, then `release` only when discarding temporary code).

These policies keep the “device is the computer” workflow intact while making memory behavior more legible.


### Expression completeness and multi-line input

Direct Mode MUST handle multi-line input by accumulating bytes until the reader reports a complete expression.

**Completeness rules (normative):**
- Unclosed `[` … `]` quotations are incomplete.
- Unclosed `p[` … `]` pattern literals are incomplete.
- Unclosed `( ... )` comments are incomplete (if supported).
- Unclosed string literals (if supported) are incomplete.
- An implementation MAY add completeness rules but MUST NOT accept a syntactically incomplete expression as complete.

**UI recommendation:** While accumulating, the device SHOULD display a continuation prompt (e.g., `..`).

**Atomicity rule (normative):** If the completed expression is a definition that ultimately performs `def`, then the system MUST either:

- apply the definition entirely, or
- discard it entirely (no partial word).

This follows from Froth’s atomic `def` semantics.

### Link Mode (optional, host-augmented)

Link Mode adds reliable framing, ACK/NAK responses, and definition-level tooling while preserving Direct Mode.

#### Handshake
The host requests Link Mode by sending:

- `STX` + `FROTH-LINK` + `ETX` (0x02, ASCII, 0x03)

The device replies:

- `STX` + `FROTH-LINK:OK:{version}` + `ETX`

If the device does not support Link Mode, it MUST treat the handshake bytes as ordinary input and remain in Direct Mode.

#### Framed input
Each complete expression is transmitted as:

- `STX` `{id}` `:` `{expression}` `ETX`

where `{id}` is a monotonically increasing decimal integer.

The device MUST buffer bytes between `STX` and `ETX` before evaluating.

If an `STX` is received while already buffering a frame, the device MUST discard the incomplete frame and treat the new `STX` as the start of a new frame.

#### Responses (textual, machine-parsable)
After evaluating a frame, the device MUST send exactly one response line:

- Success: `#ACK {id} {stack}\n`
- Error:  `#NAK {id} {err} {detail}\n`

`{stack}` MUST be formatted as in the stack visualization protocol (Section 5).

**Compatibility note (optional):** Implementations MAY additionally send legacy single-byte ACK/NAK (0x06 / 0x15), but the normative interface is the textual response line to avoid collisions with program output.

#### Flow control
The host MUST wait for `#ACK/#NAK` before sending the next frame (stop-and-wait). This is sufficient and robust for embedded targets.

#### Unframed input during Link Mode
Unframed input (bytes not inside STX/ETX) MUST still be accepted and evaluated as in Direct Mode. This allows mixing manual REPL use with tool-driven frames.

### Returning to Direct Mode
Link Mode ends when:

- the host sends `STX FROTH-UNLINK ETX`, or
- the connection is lost / idle timeout occurs (implementation-defined), or
- the device resets.

After Link Mode ends, the device returns to Direct Mode.

---

## Interrupt / “Ctrl-C” semantics

### CAN interrupt flag
If the console stream receives `CAN` (0x18), the implementation MUST set a VM-local **interrupt flag**.

### Safe points
The VM SHOULD check the interrupt flag at safe points:

- between token executions in the quotation interpreter,
- at each iteration of `while`,
- before returning to the prompt.

When the interrupt flag is observed, the VM MUST clear it and behave as if it executed `throw ERR.INTERRUPT`.

This provides consistent “stop runaway code” behavior in both Direct Mode and Link Mode.

---

## Persistence and boot behavior

Persistence is defined by the companion profile: **FROTH-Snapshot v0.5** (overlay dictionary model).

This spec defines *when* persistence actions occur; the snapshot format and restore semantics are defined in `FROTH_Snapshot_Overlay_Spec_v0_3`.

### Required persistence words
A conforming FROTH-Interactive implementation MUST provide:

- `save` `( -- )` — atomic save of the current overlay
- `restore` `( -- )` — restore overlay from snapshot
- `wipe` `( -- )` — erase snapshots and return to base-only state

### Boot sequence (normative)
On power-up:

1. Initialize Froth VM.
2. Register base words/primitives and FFI.
3. If a valid snapshot exists: restore overlay (FROTH-Snapshot v0.5).
4. If `autorun` is bound: execute `[ 'autorun call ] catch`.
5. Enter Direct Mode prompt.

### Safe boot / autorun rescue (recommended)
To rescue from a bad `autorun` (infinite loop), implementations SHOULD provide at least one of:

- a hardware safe-boot strap/pin to skip autorun,
- a short “serial break window” where CAN interrupts autorun,
- a watchdog escape to prompt.

---

## Required on-device introspection

A self-sufficient device environment requires introspection.

### Required
- `words ( -- )` — list bound slot names
- `see ( slot -- )` — display slot definition (quote tokens or `<primitive>`)
- `.s ( -- )` — print data stack without modification
- `. ( n -- )` — print signed integer (no heap allocation; uses a small fixed scratch buffer internally)

### Recommended
- `cr ( -- )` — emit a newline (recommended convenience)
- `info ( -- )` — version, slot count, free heap, snapshot status
- `slot-info ( slot -- )` — contract, version, origin (base/overlay), impl kind
- `free ( -- n )` — free heap bytes
- `used ( -- n )` — used heap bytes

---

## REPL Stack Visualization Protocol (summary)

A Direct Mode evaluation SHOULD conclude by printing the data stack in a consistent, parseable format:

- `[v0 v1 ... vN]` where v0 is bottom, vN is top.

Implementations SHOULD format non-number values compactly:

- `<q:N>` quotation reference
- `<s:name>` slot reference
- `<p:N>` pattern reference

(Full details are in the unified Froth spec’s REPL/inspection section.)

---

## Host tooling (informative)

A recommended host tool, `froth-link`, can provide:

- framing + retransmit on error
- `.froth` file send and watch mode
- definition-level diffing
- IDE integration

Host tooling MUST be optional and MUST not be required for normal operation.