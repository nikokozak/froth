# Froth Implementation Timeline

*Last reviewed: 2026-03-22 (ADR-048 complete: device, host, extension. POSIX proven. ESP32 bench remaining.)*
*Source: Froth Implementation Roadmap v0.5 (Feb 25 → Thesis deadline Apr 20)*

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

### Mar 16 (Sun) — AI-assisted host buildout (was Mar 19–21)
- [x] CLI commands: doctor (Go, cmake, make, serial, ESP-IDF, device), build (POSIX + ESP-IDF), flash (ESP-IDF + port detect)
- [x] ADR-035: daemon architecture (RPC-only Phase 1, PTY Phase 2, write serialization, CLI routing)
- [x] Daemon skeleton: Unix socket, JSON-RPC 2.0, serial ownership, reconnect, event broadcast, start/stop/status
- [x] CLI daemon routing: auto-detect socket, --serial/--daemon flags, info+send work through daemon
- [x] Code review: 6 concurrency/safety issues found and fixed (nil-map panic, broadcast deadlock, unbounded reconnect, double-close, stale PID, SetReadTimeout error)
- [x] VS Code extension skeleton (connect, send selection, console panel) — landed Mar 17
- [x] Codex review + 5 fixes (socket errors, concurrent connect, error_code fallback, id validation, buffer cleanup)

### Mar 17 (Mon) — ESP32 NVS persistence + ADR-037
- [x] ESP32 NVS snapshot backend: `nvs_flash_init`, read/write/erase with offset-based blob slicing
- [x] `froth_repl_start` restored: blocking REPL for non-link targets
- [x] `FROTH_HAS_LINK` gate in `froth_boot.c`: mux vs blocking REPL
- [x] ESP-IDF CMake parity: `nvs_flash` dependency, `FROTH_BOARD_NAME`, `FROTH_HAS_SNAPSHOTS`, `FROTH_SNAPSHOT_BLOCK_SIZE`
- [x] POSIX save/restore verified through both mux and `froth_repl_start` paths
- [x] ADR-037: host-centric deployment model (embedded user program as overlay, `reset` primitive, editor workflow, snapshot hash)
- [ ] **Proof**: flash ESP32, define word, save, power cycle, verify persistence

### Kernel work (Mar 18, priority order)
- [x] Fix ESP32 NVS serialization bug (stack overflow in platform read/write — static staging buffer)
- [x] **Proof**: flash ESP32, define word, save, power cycle, verify persistence. A/B rotation, wipe, multiple saves all work.
- [x] `dangerous-reset` primitive (ADR-037, ADR-041: strict bare identifiers fix)
- [x] `RESET_REQ`/`RESET_RES` protocol messages (ADR-037: prerequisite for honest Send File)
- [x] CS trampoline executor (ADR-040: replace C recursion, O(1) C stack, portable call depth)

### Host tooling hardening (Mar 18)
- [x] ESP32 link layer enabled (`FROTH_HAS_LINK`, transport/link/mux sources, binary-safe UART)
- [x] ESP32 `platform_key` transparent (no 0x03 interception), mux owns byte classification
- [x] Daemon rewrite: per-request waiter, no eval timeout, `writeMu` for interrupt safety, `disconnectCh`
- [x] Resilient HELLO probe: retry/resync, `ResetInputBuffer`, `/dev/cu.*` only discovery
- [x] Chunked eval: depth-aware line splitting for >253 byte sources
- [x] Extension: running state, `requireIdle` gate, fire-and-forget sendFile, fresh-connection interrupt
- [x] `key` prim: throws ERR.INTERRUPT on 0x03 (ESP32) and SIGINT (POSIX), cross-platform consistent
- [x] Session path: no-timeout eval, `CommandTimeout` for info/reset, `errors.Is` for timeout detection
- [x] Spec v0.6: Link Mode updated from STX/ETX to COBS binary framing, interrupt semantics clarified
- [x] **Proof**: all paths tested (POSIX REPL, POSIX socat link, ESP32 direct serial, ESP32 daemon)

### Extension UX and local target (Mar 18, ADR-042)
- [x] Extension rewrite: native VS Code action surfaces, viewsWelcome, lazy daemon start
- [x] Local POSIX target: daemon `--local`, `localTransport` via stdin/stdout pipes
- [x] Background daemon: `froth daemon start --background` with Setsid + re-exec
- [x] Status: `target` and `reconnecting` fields, target-aware UI labels
- [x] **Proof**: serial device and local POSIX both tested via daemon (eval, string, reset)

### Host tooling hardening tranche (Mar 19)
- [x] Shared host transport helpers: direct CLI sessions and daemon backends now use the same frame-read and HELLO probe path
- [x] Shared lexical chunker: moved into `internal/session`, with tests for comments, strings, patterns, escapes, and oversized top-level forms
- [x] Daemon lifecycle hardening: stale-socket probe, pid written after bind, background start waits for ready and prints pid only when ready
- [x] Daemon status contract: `pid`, `api_version`, and `daemon_version` exposed through JSON-RPC and CLI `daemon status`
- [x] Extension supervision refactor: dedicated daemon supervisor owns start/stop/restart, owned-PID shutdown, local runtime selection, and API-version validation
- [x] Local POSIX runtime reliability: explicit `--local-runtime`, repo/PATH fallback order, unbuffered non-TTY stdio, daemon accepts `status` before handshake completes
- [x] Notification hardening: dropped console output is now surfaced with an explicit warning instead of silent loss
- [x] **Proof**: local POSIX daemon tested in foreground and background; `status`, `info`, `send`, and `reset` all pass through the daemon

---

## Phase 3: Thesis Push (Mar 20 — Apr 20)

> Phase 3 shifts from kernel correctness to ecosystem breadth and public readiness.
> Goal: by Apr 20, Froth is a credible embedded language with multi-target support, growing HAL coverage, a library story, and polished tooling.

### Phase 3a: Tooling System (Mar 19–21) — COMPLETE

- [x] User programs (`FROTH_USER_PROGRAM`)
- [x] LEDC/PWM raw FFI (12 words) + Froth convenience layer (4 words)
- [x] I2C raw FFI (10 words with handle tables)
- [x] FFI metadata limit bumped to 8
- [x] Transient string buffer (ADR-043)
- [x] Board lib auto-embed infrastructure
- [x] SDK embedding in CLI binary
- [x] Project system (ADR-044): manifest, resolver, CLI wiring
- [x] `froth new`, `froth build`, `froth flash` (manifest-driven)
- [x] `froth send` with include resolution
- [x] `froth connect --local` (zero-setup POSIX REPL)
- [x] `froth connect` serial (RPC-backed interactive session)
- [x] Extension sendFile delegates to CLI
- [x] VS Code syntax highlighting
- [x] Manifest-aware `froth doctor`
- [x] `froth build --clean`
- [x] ESP-IDF build isolation (per-project staging)
- [x] Test battery: ~90 tests (kernel shell tests, Go CLI unit tests, Go integration tests, `make test`)
- [x] `dangerous-reset` clears tbuf + interrupted flag
- [x] Chunk scanner fix (backslash token matching)

### Phase 3a-kernel: Kernel Hardening (Mar 21–22, POSIX, no hardware) — COMPLETE

- [x] `catch` truth convention ADR + kernel fix (ADR-045, landed Mar 21)
- [x] `n>s`, `n>hexs`, `n>bins` — number-to-string primitives (ADR-046, landed Mar 21)
- [x] `s.concat` — dynamic string building (landed Mar 21)
- [x] Unified `FROTH_STRING_MAX_LEN` (ADR-047, landed Mar 21) — replaces `FROTH_BSTRING_LEN_MAX`, default 256, CMake-configurable, enforced at all creation points
- [ ] Blob pool — deferred to post-workshop (WiFi/HTTP phase)
- [ ] `froth_invoke` FFI callback API — deferred to post-workshop (WiFi/HTTP phase)

### Phase 3a-bugfix: Host Tooling Bugfixes (Mar 22)

- [x] Daemon transport read loop: returns to console mode after frame delivery (was stuck in frame mode)
- [x] Interrupt cancels in-flight eval waiter (`ErrEvalInterrupted`, `interruptibleWaiter` struct)
- [x] File/manifest send does reset before eval per ADR-037 (raw source stays eval-only)
- [x] Tests: frame-mode-exit regression, interrupt-cancel focused test, send_test coverage updated
- [x] **Proof**: `cd tools/cli && go test ./...` passes

### Phase 3a-transport: Exclusive Live Session Transport (ADR-048, Mar 22–26)

> IMMEDIATE PRIORITY. Replaces mixed-stream mux (ADR-033/034) with exclusive
> Direct/Live two-mode model. Fixes architectural root cause of daemon hangs
> and serial corruption. Device and host work proceed in parallel after step 2.

#### Shared foundation — DONE (Mar 22)
- [x] V2 frame header (20 bytes: magic, version, type, session_id u64, seq u16, payload_len u16, crc32) — C and Go
- [x] New message type constants — C and Go (renumbered, INSPECT/EVENT removed)
- [x] `platform_uptime_ms()` — POSIX + ESP32

#### Device side (kernel) — DONE (Mar 22, two review passes clean)
- [x] Bounded Direct Mode recognizer (`froth_console.c`): 64-byte cap, 50ms timeout, HELLO_REQ + ATTACH_REQ
- [x] HELLO_REQ/RES in Direct Mode: stateless discovery, no state transition
- [x] ATTACH/DETACH state transitions: precondition checks, ATTACH_RES OK/BUSY/INVALID
- [x] Live frame dispatch loop: frame-only I/O, session_id + seq validation, DETACH
- [x] Output buffering: `froth_console_emit` shim, flush on `\n` / full / before terminal frames
- [x] EVAL/INFO/RESET handlers ported to v2 framing
- [x] Live poll hook (`froth_console_poll`): executor safe points, KEEPALIVE + INPUT_DATA + INTERRUPT_REQ with seq discipline
- [x] Input FIFO + key/key?: 64-byte FIFO, INPUT_WAIT edge-triggered, blocking wait with poll
- [x] Lease timer: refreshed on valid frames only, expiry returns to Direct (interrupts eval)
- [x] INTERRUPT_REQ: sets `vm->interrupted`, seq-validated against active_seq
- [x] Feature gating: `FROTH_HAS_LIVE` CMake define, `FROTH_HAS_LINK` removed, Direct-only build verified
- [x] Review pass 1: seq validation, strict frame length, error frames, flush ordering (5 fixes)
- [x] Review pass 2: non-blocking idle, poll seq discipline, helpers extracted (no new issues)
- [x] `froth_console_mux.c` replaced, `froth_repl_is_idle()`, `froth_link_send_hello_res()` extracted
- [x] **Proof**: POSIX round-trip — attach, eval, OUTPUT_DATA, interrupt, key+INPUT_WAIT+INPUT_DATA, detach (5 integration tests, Mar 22). Lease expiry deferred to ESP32 bench.
- [ ] ESP32 validation: same tests on real hardware

#### Host side (daemon + CLI + extension) — DONE except extension (Mar 22)
- [x] Protocol package: v2 header, new message builders/parsers, GenerateSessionID, ParseAttachResponse, ParseOutputData, ParseInputWait, BuildInputDataPayload
- [x] HELLO probe updated for v2: Direct Mode discovery, no v1 fallback
- [x] Session lifecycle: daemon lazy attach, session_id, sequential seq (wraps 0xFFFF->1), attach/detach
- [x] Simplified transport read loop: frame-only after attach, no byte classification, non-frame bytes discarded
- [x] Console events from OUTPUT_DATA (replaces raw-byte accumulation)
- [x] KEEPALIVE timer: fire-and-forget every 2s
- [x] INTERRUPT_REQ replaces raw 0x03 (seq=activeSeq, 5s safety cancel)
- [x] INPUT_DATA sending: `input` RPC, daemon `deviceSendInput`, client `SendInput`
- [x] Session failure + recovery: disconnect clears session state, reconnect re-probes HELLO
- [x] CLI commands: send/reset use attach/detach + OutputHandler, info uses cached HELLO
- [x] Direct serial session migrated: same attach/detach/seq/KEEPALIVE, inline OUTPUT_DATA
- [x] v1 mixed-stream code removed, API version 2
- [x] Review pass: shutdown detach timeout, disconnect ordering, waiter type validation, frameBuf cap
- [x] POSIX EOF spin fix: orphaned runtimes exit cleanly on broken stdin
- [x] **Proof**: daemon + CLI end-to-end on POSIX (5 tests: info, eval+output, reset+eval, interrupt, key+input)
- [x] Extension: API version 2, console from OUTPUT_DATA (no change needed), INPUT_WAIT → input box + sendInput (Mar 22)

### Phase 3a-hw: Hardware Validation + New Bindings (interleave with transport, bench days)

Smoke tests on real ESP32 hardware:
- [ ] LEDC/PWM: LED fade, piezo tone, convenience words
- [ ] I2C: sensor read (temperature or accelerometer)
- [ ] UART bindings: `uart.init`, `uart.write`, `uart.read` (new FFI words)
- [ ] `millis` — uptime counter (ESP32: `esp_timer_get_time`, POSIX: `clock_gettime`)
- [ ] ADC: `adc.read ( pin -- value )` (new FFI word)
- [ ] User program cold boot on ESP32, snapshot priority, wipe cycle
- [ ] CLI `froth send` with includes → ESP32 device (end-to-end, using Live transport)
- [ ] CLI `froth build` + `froth flash` with froth.toml project
- [ ] VS Code extension Send File → ESP32 (via Live session)
- [ ] Flash 15 boards with user program, test workshop flow
- [ ] **Workshop (first week of April)**

### Phase 3b: WiFi + HTTP (post-hardware-validation)

- [ ] WiFi bindings: `wifi.connect` (SSID + password strings), `wifi.status`, `wifi.ip`. Uses string bridge + blob pool.
- [ ] `http.get ( url -- status body )` — materializes response as transient/blob string. Error if too large.
- [ ] HTTP server: `http.serve ( port handler -- )` — uses `froth_invoke` to call handler quotation per request. Serves user-defined HTML pages.
- [ ] `http.get-stream ( url callback -- status )` — stream-oriented for large responses. Callback receives chunks.
- [ ] Phone-controllable demo: ESP32 serves a web page with buttons, buttons trigger Froth eval.

### Phase 3c: Second Target + Demo (Apr 7–12ish)

- [ ] RP2040 platform port: `platform.c` (USB-CDC), board FFI (GPIO, PWM), Pico SDK CMake.
- [ ] One ported library using the include system.
- [ ] Thesis demo project: WiFi + I2C or PWM + persistence + library system + host tooling.

### Phase 3d: Polish + Thesis Prep (Apr 12–16)

- [ ] Demo project polished and reliable under presentation conditions
- [ ] Getting started guide: flash, connect, write first program, include a library, deploy
- [ ] Thesis chapter: architecture, design decisions, comparison to ESP32forth/MicroPython/Lua
- [ ] Error location mapping in CLI (source file + line from boundary markers)

### Buffer (Apr 17–20)

Presentation prep. Fix anything that broke. Practice the demo.

## Kernel "Definition of Done"

- [x] No GC
- [x] No implicit allocation during primitive execution
- [x] Coherent redefinition works
- [x] Errors recover to prompt
- [x] `perm` passes tests

## Workshop "Definition of Done"

- [x] LED blink from Froth REPL on ESP32
- [x] Interrupt stops runaway loop
- [x] `save` survives power cycle on ESP32
- [x] `wipe` returns to base-only state
- [x] `"Hello" s.emit` works
- [x] Hex literals work (`0xFF`)
- [x] Host tooling can push code to device (CLI or VS Code via FROTH-LINK/1, migrating to Live session ADR-048)
- [x] User program boots on cold start, `reset` + Send File replaces it (proven on POSIX)
- [x] Project system: `froth.toml`, include resolution, `froth new/build/send/flash`
- [x] CLI complete: `connect --local`, `connect` serial, `doctor`, `--clean`, SDK embedding
- [x] Extension delegates to CLI for include resolution
- [x] Test battery: ~90 tests across kernel, CLI, and integration layers
- [ ] LEDC/PWM bindings proven on ESP32 hardware (LED fade or piezo tone)
- [ ] I2C sensor read proven on ESP32 hardware
- [ ] 15 pre-flashed ESP32 boards ready

## Thesis "Definition of Done"

- [ ] Two targets running Froth (ESP32 + RP2040)
- [ ] WiFi or network-controllable demo (phone controls hardware)
- [x] Library/include system working end-to-end (ADR-044, 42 resolver tests, integration tests)
- [x] VS Code syntax highlighting for `.froth` files
- [x] Project system (`froth new/build/send/flash/connect/doctor`)
- [x] Test battery (~90 tests across all layers)
- [ ] One ported library included and used in a project
- [ ] Non-trivial demo project running on real hardware
- [ ] Getting started guide exists
- [ ] `catch` truth convention resolved

## Deferred (post-thesis)

### Kernel deepening

- **CALL tag decoupling** (ADR TBD): Move CALL/literal distinction out of the value-tag layer (ADR-009 rework). Frees tag 6 for NativeAddr. Prerequisite for FROTH-Addr. Independent cleanup.
- **FROTH-Addr profile** (ADR-024): Native address type for full-width machine addresses. Fixed address pool, width-specific memory access, FFI API additions. Target: post-thesis when doing direct register work.
- FROTH-String (`s.pack` — explicit allocation from FFI buffers, `\0` support for binary buffers)
- `/mod` overflow: make wrapping behavior normative in spec (ADR-011).
- Streaming snapshot serializer v2 (ADR-038: ~344B writer, ~280B reader). Current static workspace is a known debt, not a blocker.
- Evaluator refactor: split `froth_evaluator.c` into `froth_toplevel.c` + `froth_builder.c`.

### Build architecture

- **Chip-level peripheral layer** (ADR TBD): chip-generic FFI modules (LEDC, I2C, SPI, WiFi) currently live in board directories but aren't board-specific. Introduce `chips/esp32/` (or `hal/esp32/`) for peripherals that work on any board using that chip. Boards compose their FFI surface from chip modules + board-specific pin/config words. Affects CMake, boot registration, and how RP2040 peripherals are organized. Target: immediately after Phase 3a.

### Tooling deepening

- Daemon PTY passthrough (Phase 2 of ADR-035)
- Async eval model: `eval.start` returns ack, completion arrives as event
- Shared host request engine unification (daemon + direct CLI collapse). Defer until after manual editor validation, so any remaining UX bugs are not mixed with the structural refactor.
- Daemon/supervisor integration tests (API mismatch, ownership, wrong-mode restart)

### Language maturation

- DTC/native promotion (FROTH-Perf)
- Named frames compiler pass (FROTH-Named); consider a "Named Lite" path first
- Checked kinds/contracts as selectable build profile (FROTH-Checked)
- FROTH-Region-Strict (fail-fast allocation gating)
- Step mode / trace mode for debugging
- Richer `see` (pretty printing, source retention policies)
- Stack effect and help text metadata for user-defined words

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
| AI-assisted host buildout | Mar 19–21 | Mar 16 | CLI commands + daemon landed same day as skeleton. ADR-035, 6 review fixes. VS Code still pending. |
| ESP32 NVS persistence | Mar 16–21 | Mar 17 | NVS backend written and compiles. Hardware test pending (Mar 18). |
| Host-centric deployment ADR | Not scheduled | Mar 17 | ADR-037 accepted. Defines deployment model, reset primitive, editor workflow. |
| ESP32 audio FFI | Mar 16–21 | Dropped | Pivoted to HAL breadth (LEDC/I2C/WiFi) for thesis punch. Piezo tones available via LEDC. |
| Workshop | "week of Mar 15" | first week of Apr | NYU faculty strike. Extended scope: full tooling system landed before workshop. |
| Tooling system | Mar 20–23 | Mar 19–21 | Landed early. ADR-043, ADR-044, SDK embedding, all CLI commands, extension wiring, test battery. Codex-implements/Claude-reviews workflow adopted. |
| Library/include system | Phase 3b | Mar 20–21 | Pulled forward into Phase 3a. ADR-044 accepted, resolver + CLI wiring + 42 tests. |
