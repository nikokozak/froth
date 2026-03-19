# Host Tooling Architecture Review

**Date:** 2026-03-19  
**Scope:** Go daemon, Go CLI host path, VS Code extension  
**Inputs reviewed:** `PROGRESS.md`, `TIMELINE.md`, `docs/spec/Froth_Interactive_Development_v0_5.md` v0.6, ADR-033, ADR-034, ADR-035, ADR-039, ADR-042, host tooling implementation under `tools/cli/` and `tools/vscode/`

## Session start context

Per `PROGRESS.md` (updated 2026-03-18), the project is in the **Ecosystem** phase. The kernel and firmware link path are largely proven; the next work is host-side hardening. Per `TIMELINE.md`, the kernel phase is done and the host tooling is now the critical path.

The next logical step is **not** more editor features. It is to harden the host control plane so that daemon lifecycle, transport ownership, local-target startup, version compatibility, and file-send behavior become predictable.

## Bottom-line judgment

The chosen architecture is broadly correct.

The major decisions are good:

- device-first principle
- COBS framed link layer on the same console stream
- daemon as the single transport owner
- thin editor talking JSON-RPC to the daemon
- local POSIX target running through the same daemon contract as hardware

I would **not** recommend:

- rewriting the daemon in C
- moving serial ownership into the VS Code extension
- abandoning the daemon/editor split

Those changes would attack the wrong problem. The current pain is coming less from the top-level architecture than from weak contracts and blurred boundaries inside the host implementation.

That said, the current host stack is **not yet robust enough for a novice-facing open source release**. The weak points are concrete and fixable, but they need to be treated as primary engineering work, not cleanup.

## What is good

### 1. The architectural shape is proportionate to the problem

ADR-035 and ADR-042 make the right call: the extension should stay thin and the daemon should own transport concerns. Putting serial and reconnection logic in TypeScript would create a worse system, not a simpler one.

### 2. The device/host protocol boundary is clean

ADR-033 and ADR-034 are the strongest part of the host story. COBS framing plus a console multiplexer is a real protocol, not a scrape of human text. That is the right long-term move.

### 3. Local mode through the daemon is the right abstraction

ADR-042 is correct to reject a hybrid extension design. One JSON-RPC control surface for both serial and local targets is simpler than two completely different execution paths.

### 4. Go is a sensible implementation language here

The daemon is doing process management, socket serving, reconnection, and transport multiplexing. C would not make that simpler or more reliable. It would just make memory safety and concurrency mistakes more expensive.

## Where the system is actually failing

### 1. The host core is duplicated instead of shared

The current code duplicates core transport logic between the direct CLI session path and the daemon path.

Examples:

- eval chunking is duplicated in [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 815-854 and [`tools/cli/internal/session/session.go`](../tools/cli/internal/session/session.go) lines 194-233
- HELLO probing exists in both [`tools/cli/internal/serial/discover.go`](../tools/cli/internal/serial/discover.go) lines 85-153 and [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 543-598
- frame-read logic exists in both [`tools/cli/internal/serial/port.go`](../tools/cli/internal/serial/port.go) and [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 600-637

This is the opposite of the intended architecture in the tooling proposal. The project wanted a reusable host core. In practice it now has two host stacks that happen to speak the same protocol.

That creates drift risk immediately:

- a bug fix lands in one path but not the other
- chunking behavior diverges between direct CLI and daemon
- handshake hardening diverges between transports

This is one of the main reasons the system feels brittle even though each part is individually small.

### 2. Daemon lifecycle and singleton ownership are not hard enough

The daemon currently writes the PID file and then unconditionally removes the socket path before binding:

- [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 363-375

That is not a safe singleton protocol. If an old daemon is alive but the extension cannot talk to it cleanly, a new daemon can clobber the visible socket path and PID file without first proving the old process is gone. That is exactly the sort of behavior that produces "haunted" tooling: the user thinks there is one daemon, but there may be two processes and one orphaned serial owner.

The extension side is also looser than the ADR promises. ADR-039 says ownership should be tracked via PID-aware logic. The current extension does not do that. It just tries to connect and, on failure, starts a daemon and marks itself owner:

- ADR intent: [`docs/adr/039-host-tooling-ux-and-daemon-lifecycle.md`](../docs/adr/039-host-tooling-ux-and-daemon-lifecycle.md) lines 40-45
- current extension behavior: [`tools/vscode/src/extension.ts`](../tools/vscode/src/extension.ts) lines 358-483

This means ownership is inferred from a transient connection outcome, not from a durable contract.

For a robust release, the daemon must have a real singleton/ownership model:

- explicit lock or lease, not socket removal as the primary arbiter
- ready/healthy state distinct from merely "socket file exists"
- owner token or PID-based contract the extension can reason about

### 3. Version compatibility is underspecified in the places that matter

The device protocol has versioning. The host control plane effectively does not.

Current `status` returns:

- connected/disconnected
- reconnecting
- target
- device hello info

See [`tools/cli/internal/daemon/rpc.go`](../tools/cli/internal/daemon/rpc.go) lines 247-271.

What is missing:

- daemon API version
- daemon build/version
- supported device protocol version range
- extension/daemon compatibility check
- daemon/device capability compatibility check beyond a passive HELLO payload

This matters because your stated fears are correct: once CLI, daemon, extension, and firmware are versioned separately, "it sort of connects but behaves strangely" becomes the default failure mode unless compatibility is explicit and checked early.

### 4. Syntax-sensitive eval chunking lives in the wrong layer, and it is too heuristic

Both daemon and direct session chunk large sends by scanning text and tracking only `[`, `]`, `:`, and `;`.

See:

- [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 812-854
- [`tools/cli/internal/session/session.go`](../tools/cli/internal/session/session.go) lines 188-233

This is not a full Froth reader. It does not understand:

- strings
- nested comments
- the real top-level form boundary rules

So a host concern has become a partial duplicate of language syntax.

This is a structural smell. Either:

1. chunking must be centralized in one host-core reader that is intentionally kept in sync with the language, or
2. better, chunking must stop being a host syntax problem and become a protocol feature

For example, a future `eval.begin` / `eval.chunk` / `eval.commit` flow or an upload-form request would be cleaner than maintaining ad hoc syntax tracking in multiple host modules.

### 5. Local POSIX target discovery is heuristic when it should be explicit

`findLocalBinary()` guesses:

- `./build64/Froth`
- `Froth` on PATH
- `froth` on PATH if it is not the CLI itself

See [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 178-200.

This directly matches the pain you described. The local target is not represented as a first-class configured runtime. It is guessed from path conventions.

That is acceptable for developer convenience, but not as the release story.

A robust system needs:

- explicit local runtime path support in CLI and extension config
- an explicit error when the configured runtime is missing or stale
- optional fallback guesses for development, but never as the primary contract

### 6. The daemon implementation is carrying too many responsibilities in one module

`tools/cli/internal/daemon/daemon.go` is 1135 lines. It currently contains:

- daemon lifecycle
- serial transport
- local child-process transport
- handshake/probe logic
- read-loop frame demux
- reconnect loop
- request serialization
- eval chunking
- device RPC implementation

That is not an architecture failure by itself, but it is a strong maintainability warning. The project chose Go partly to make host complexity manageable. In practice, the daemon core is now dense enough that boundary mistakes are easy to make and hard to review.

### 7. There are implementation leaks that signal insufficient hardening

Two specific examples:

- `rpcConn.notifyLoop()` is started per client, but ordinary client disconnect does not close `notifyCh`; the goroutine can leak after `serve()` returns because `acceptLoop()` removes the client from the map without calling `c.close()`. See [`tools/cli/internal/daemon/rpc.go`](../tools/cli/internal/daemon/rpc.go) lines 124-146 and [`tools/cli/internal/daemon/daemon.go`](../tools/cli/internal/daemon/daemon.go) lines 743-770.
- notifications are intentionally dropped when a client is slow. See [`tools/cli/internal/daemon/rpc.go`](../tools/cli/internal/daemon/rpc.go) lines 294-304. This is acceptable for best-effort console text, but it means the daemon is not yet operating with hard delivery guarantees even inside its own control surface.

These are implementation issues, not reasons to scrap the design, but they are signs that the current host layer is not yet disciplined enough to be trusted by novices.

### 8. The testing story is upside down

What exists:

- `go test ./...` passes, but there are effectively no Go tests
- the TypeScript daemon client has a good mock-daemon smoke test

What is missing:

- daemon lifecycle tests
- reconnect tests
- singleton/ownership tests
- serial-vs-local transport parity tests
- extension integration tests against a real daemon stub

This is the central sustainability problem. The fragile parts are exactly the parts with the least verification.

## Pushback on the instinct to rewrite

Your instinct that "this is too complex for what it does" is partly right, but the wrong conclusion is easy to draw from that.

The problem is not that a daemon exists. The problem is that the daemon is currently trying to be:

- a singleton service
- a transport multiplexer
- a reconnect manager
- a local target launcher
- a protocol client
- a lifecycle authority

without those roles being cleanly factored.

Rewriting it in C would not reduce conceptual load. It would just remove memory safety and make socket/process bugs more punishing.

Likewise, pushing more logic into the extension would make the system less robust, not more. The TypeScript layer should remain a UI shell.

## What I would change

### Immediate: freeze the scope of "supported host workflows"

Before adding PTY, async eval, sync ledgers, or richer editor semantics, freeze the supported workflow to:

- connect
- status
- eval
- reset
- interrupt
- console stream
- local POSIX target

Everything else should be explicitly secondary until this control plane is boring.

### Immediate: make daemon lifecycle explicit and enforce singleton rules

Add a daemon lifecycle contract and ADR for:

- singleton lock semantics
- ownership token or PID-based owner contract
- ready state vs running state
- stop semantics
- target mode changes

The extension should not decide ownership from "I failed to connect so I guess I own the next daemon."

### Immediate: introduce daemon API versioning

Add daemon API version/build info to `status`, and have the extension reject incompatible daemon versions early with a precise message.

Then add device capability/protocol compatibility checks in the daemon after HELLO, also with explicit user-facing errors.

### Near-term: extract a real host core

Refactor toward:

- `internal/link` or similar for shared frame/request logic
- shared handshake/probe code
- shared eval send/chunk logic
- transport interface implemented by serial and local backends

The direct CLI path and daemon path should use the same request machinery.

### Near-term: make local runtime selection explicit

Add a first-class local runtime path setting and CLI flag. Keep `./build64/Froth` as a development fallback only.

Without this, local mode will keep feeling magical and fragile.

### Near-term: replace heuristic chunking or confine it

Preferred long-term direction:

- make large-send chunking a protocol feature, not an editor/daemon text heuristic

Acceptable short-term direction:

- centralize chunking in one shared host-core function
- make it a real lexical state machine that matches Froth reader completeness rules

### Near-term: add daemon integration tests before new features

At minimum:

- start/stop/singleton tests
- local target launch test
- reconnect test
- eval during console output
- interrupt during long-running eval
- wrong-mode switch test

The daemon is the product boundary. It needs the best tests, not the fewest.

## Practical release guidance

If you want to release soon, I would not release "the full Froth IDE story" as if it were stable.

I would release in layers:

1. firmware + CLI as the supported core
2. daemon as supported but still young
3. VS Code extension as preview/experimental until lifecycle and compatibility hardening land

That is not a retreat. It is honest staging.

## Concrete determinations

### Does the chosen architecture serve the project?

**Yes, in broad shape. No, in current host implementation discipline.**

The daemon-plus-extension architecture is compatible with the project's goals. The current implementation does not yet meet the robustness standard those goals require.

### Is a rewrite justified?

**No full rewrite.**

Do not rewrite the daemon in C. Do not collapse the daemon into the extension. Do not replace the protocol stack.

### Is a significant refactor justified?

**Yes.**

The refactor should target:

- lifecycle/singleton rules
- shared host core
- compatibility contracts
- explicit local runtime configuration
- test coverage around the daemon boundary

## Recommended next ADRs

I would write at least two ADRs before continuing major host work:

1. **Host daemon singleton, ownership, and readiness contract**
2. **Host eval/upload contract**
   Choose between shared lexical chunking and protocol-level streaming/upload

If local mode is going to be a first-class release feature, I would also add:

3. **Local runtime discovery and configuration contract**

## Final recommendation

Keep the architecture. Narrow the surface area. Harden the contracts. Refactor the host core so the daemon and direct CLI are not independent implementations of the same protocol behavior.

The current system feels fragile because too much correctness depends on convention, heuristics, and timing. That is fixable without abandoning the direction.
