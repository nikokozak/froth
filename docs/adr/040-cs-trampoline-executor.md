# ADR-040: CS Trampoline Executor

**Date**: 2026-03-17
**Status**: Accepted
**Spec sections**: Froth_Core (quotation execution), ADR-031 (call depth guard)

## Context

The executor (`froth_executor.c`) uses C recursion for nested word calls. When a quotation body contains a `FROTH_CALL` tag, `froth_execute_quote` calls `froth_execute_slot`, which may call `froth_execute_quote` again. Each nesting level consumes a C stack frame (~40-80 bytes depending on platform and compiler).

ADR-031 added a `call_depth` guard (default 64) to prevent C stack overflow. This converts a segfault into a catchable error, but the underlying problem remains: the maximum call depth is determined by the C stack size, which varies across platforms and is invisible to the user.

On ESP32 (8KB default task stack), 64 levels is survivable. On RP2040 or smaller targets, it may not be. The C stack budget is shared with primitives, FFI callbacks, and platform code. Deep nesting competes with all of them.

The CS (call stack) exists on the VM but is currently unused. It was reserved for exactly this purpose.

## Options Considered

### Option A: Keep C recursion, tune the limit per target

Each target sets `FROTH_CALL_DEPTH_MAX` based on its known stack size. ESP32 gets 64, RP2040 gets 32, small targets get 16.

Trade-offs:
- Pro: zero implementation work.
- Con: the limit is a guess. It depends on compiler optimizations, primitive stack usage, and FFI callback depth. A safe limit on one build may crash on another with different flags.
- Con: the failure mode is still opaque. Users don't control C stack size.

### Option B: Trampoline loop with CS frames

Replace C recursion with an explicit loop. When the executor encounters a `FROTH_CALL`, it pushes a continuation frame onto the CS (saving where to resume in the current quotation) and starts executing the callee. When a quotation body ends, the executor pops a CS frame and resumes the caller. The top-level loop runs until the CS is empty.

Trade-offs:
- Pro: C stack depth is O(1) regardless of Froth call depth.
- Pro: maximum call depth is `FROTH_CS_CAPACITY`, which is CMake-configurable, explicit, and platform-independent.
- Pro: failure mode is a clean `FROTH_ERROR_CALL_DEPTH` from a stack bounds check, not a segfault.
- Con: implementation work. The executor loop changes from a simple `for` over body cells to a state machine.
- Con: slight overhead per call (CS push/pop instead of C call/return). Negligible for an interpreter.

### Option C: Tail-call optimization only

Keep C recursion but detect tail calls (last cell in a quotation body is a `FROTH_CALL`) and reuse the current frame. Reduces C stack usage for tail-recursive patterns.

Trade-offs:
- Pro: helps the common recursive case (e.g., `while`-like patterns defined in Froth).
- Con: does not help non-tail calls. Deep mutual recursion still blows the C stack.
- Con: adds complexity without solving the fundamental problem.

## Decision

**Option B.** The CS trampoline replaces C recursion entirely. The executor becomes a single loop that manages its own call stack.

### CS frame format

Each frame is two cells:

1. **Resume offset**: heap byte offset of the next cell to execute in the parent quotation.
2. **End offset**: heap byte offset one past the last cell in the parent quotation body.

Two `froth_cell_t` values per frame. At 20 frames (typical `FROTH_CS_CAPACITY`) on a 32-bit target, that's 160 bytes. Not worth a packing scheme.

The CS entry type changes from `froth_cell_t` to a two-cell struct. The existing `froth_stack` infrastructure can be reused if the stack element type is widened, or the CS can use its own purpose-built push/pop with the struct type.

### Executor loop

```
push initial quotation as (start+1, start+1+length)

while CS is not empty:
    pop frame (resume, end)
    for cell in resume..end:
        check interrupt
        switch tag:
            literal → push to DS
            CALL → look up slot
                if prim → call C function
                if quote → push continuation (cell+1, end), push callee (start+1, start+1+length), break inner loop
                if value → push to DS
    // body finished: loop pops next CS frame (the caller's continuation)
```

When a quotation body finishes, the loop naturally pops the next CS frame, which is the caller's continuation. No explicit "return" instruction needed.

### RS balance check

The current executor checks RS depth on quotation exit (ADR-022). With the trampoline, this check moves to CS frame pop: when resuming a parent frame, verify RS depth matches what it was when the child was entered. This requires storing RS depth in the CS frame (third cell) or checking it at push/pop boundaries.

Decision: add RS depth as a third field in the CS frame. Three cells per frame, 240 bytes at 20 frames on 32-bit. Still negligible.

### call_depth removal

The `call_depth` counter on the VM becomes redundant. The CS capacity provides the same bound. `FROTH_CALL_DEPTH_MAX` is removed. `FROTH_ERROR_CALL_DEPTH` is reused for CS overflow (same user-visible meaning: "calls nested too deep").

### `while` and other loop primitives

`while` already uses a C `for(;;)` loop. It calls `froth_execute_quote` for the condition and body. With the trampoline, `froth_execute_quote` becomes the trampoline entry point. `while` calls it, the trampoline runs to completion, `while` loops. No change to `while`'s structure.

Same for `catch`, `times`, and any other primitive that calls `froth_execute_quote`.

## Consequences

- C stack usage becomes O(1) for Froth execution regardless of call depth.
- Maximum call depth is `FROTH_CS_CAPACITY` (CMake-configurable, default 20).
- The CS is no longer unused. Its element type changes from `froth_cell_t` to a three-cell struct.
- `froth_stack.h` may need a second stack type (for the wider frame), or the CS can use a purpose-built array with index. The generic stack was designed for single-cell elements.
- The executor becomes slightly more complex (loop with CS management vs simple recursion), but each piece is small and testable.
- `call_depth` field removed from VM struct. One less thing to manage in `reset`.
- All existing tests must pass unchanged. The trampoline is an implementation change, not a semantic change.
- Portability improves immediately. Smaller targets get predictable, configurable call depth without guessing C stack budgets.

## Implementation priority

After `reset` primitive (ADR-037/039). Before RP2040 porting work.

## References

- ADR-031: Call depth guard (the problem this solves permanently)
- ADR-022: RS quotation balance check (must be preserved in trampoline)
- `src/froth_executor.c`: current recursive implementation
- `src/froth_stack.h`: generic stack (may need adaptation for wider CS frames)
