# Frothy Progress

*Last updated: 2026-05-05*

This file is the thin operational note for Frothy.
The current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Landed And Still Relevant

Historical validation notes before 2026-05-05 use the old broad
`make test-frothy` contract. After Frothy ADR-126, use
`make test-frothy-full` when that previous full-host breadth is needed.

- Frothy `v0.1` is closed through M10; the dated ladder is done.
- The first embedded tool-surface tranche is now landed: Frothy no longer
  treats the accepted `v0.1` spec as the whole present-day user-facing
  ceiling, the maintained base image now ships `map`, `clamp`, `mod`, `wrap`,
  and integer `random.*` helpers plus short aliases across the maintained board
  paths, and `docs/adr/123-post-v0_1-embedded-tool-surface.md` plus
  `docs/roadmap/Frothy_Embedded_Tool_Surface_Tranche_1.md` now record that
  boundary explicitly, including the naming rule that canonical dotted
  families stay ordinary slots while bare aliases are reserved for common pure
  transforms. The default POSIX profile now carries the payload arena budget
  needed by that richer base image, and the test runner invalidates host-profile
  caches when CMake or board profile inputs change.
- The first workshop-board DRAM downsize tranche is now landed: the v4 board
  now carries an `8192`-byte heap and a `64`-frame explicit evaluator stack,
  the snapshot codec no longer keeps duplicated encode/decode tables live in
  BSS at the same time, base-slot ownership no longer costs a full pointer
  array, and `docs/roadmap/Frothy_Workshop_DRAM_Tranche_2026-04-15.md` records
  the exact baseline, scenario high-waters, byte recovery, and remaining
  payload-arena constraints.
- Spoken-ledger syntax tranche 1 is the frozen baseline for future language
  work. See `docs/spec/Frothy_Language_Spec_vNext.md`,
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, and
  `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`.
- The first workspace/image-flow tranche is closed as a doc-only,
  slot-bundle-first boundary. See
  `docs/adr/115-first-workspace-image-flow-tranche.md` and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- The direct-control surface, helper/editor path, and runtime hardening
  tranches are already landed and are no longer the unclear part of the repo
  story.
- The publishability audit is now landed at
  `docs/audit/Frothy_Repo_Audit_2026-04.md`; it now serves as the reference
  record for the landed publishability reset rather than as a future cleanup
  ledger.
- The Frothy CLI/install rename tranche is now landed: repo, product, docs,
  release assets, installed CLI, and repo-local checkout build all use the
  Frothy-owned `frothy` / `frothy-cli` names, while VS Code keeps only narrow
  legacy `froth` discovery fallback during the transition.
- The VS Code live-edit loop now separates last-run and pinned-run targets:
  `Run Binding` records a zero-arity `name:` call for `Rerun Last Form`,
  `Pin Run Binding` records a fixed `name:` call for repeated testing after
  edits, and definition or top-level value sends do not overwrite the
  remembered run target.
- The VS Code board recovery path no longer strands the editor in `running`:
  control-session prompt acquisition sends the emergency Ctrl-C much earlier,
  helper disconnect can close a serial transport even while an eval owns the
  operation lock, pending helper connects no longer block later disconnect
  recovery, full-file send interrupts and supersedes stale long evals, stale
  helper completions cannot clobber a fresh session, remembered serial ports
  fall back to discovery when stale, and `dangerous.wipe` now requires an
  explicit editor confirmation. The VS Code board proof now exercises
  interrupt, Send File while running, and disconnect while running through the
  real extension host.
- The VS Code stale-session escape hatch is now explicit rather than inferred
  from serial noise: `Frothy: Force Reconnect` hard-restarts the editor helper
  and reconnects, the running status item points at that command for reset or
  power-cycle recovery, and unsettled whole-file sends offer it without replaying
  the file across the fresh session.
- This control-surface repair tranche is landed: `PROGRESS.md` and
  `TIMELINE.md` are thin again, the repo startup guidance is task-scoped, and
  the forward queue now lives in one short roadmap note plus Frothy ADR-116.
- The workshop-operational queue is retained but no longer leads this pass:
  clean-machine validation, room-side hardware/recovery prep, and one recorded
  measured rehearsal pass remain open behind the current thesis-facing prune.
  The evaluator frame-arena ownership revisit is deferred until Frothy
  intentionally grows multiple live runtime instances or another re-entrant
  evaluator owner.
- The first workshop base-image board/library cut is landed: `millis()` and
  `gpio.read()` are now native base slots, the preflashed workshop helper
  library is seeded as base image and survives `dangerous.wipe`, and the M10 proof
  ladder now covers `blink`, `animate`, GPIO helpers, and `adc.percent`.
  Reference: `docs/adr/121-workshop-base-image-board-library-surface.md`.
- The Frothy-native TM1629 workshop board cut is now landed:
  `esp32-devkit-v4-game-board` ships a maintained TM1629 C runtime plus
  baked-in `tm1629.raw.*`, `tm1629.*`, and `matrix.*` base-image surfaces;
  board base ownership now comes from a captured install-time registry rather
  than hard-coded slot-name allowlists; and the host proof ladder now includes
  direct TM1629 runtime tests plus a POSIX sub-build smoke for the new board.
  Reference: `docs/adr/119-tm1629-board-base-surface-and-registry.md`.
- The post-review TM1629 cleanup tranche is now landed: parser, shell, and
  snapshot name validation share one Frothy grammar for `!`, `@`, and `?`;
  `tm1629.raw.init` now fails on invalid pins or failed pin-mode setup instead
  of silently succeeding; the payload-fragmentation proof scales with
  board-configured arena size; the ESP-IDF v4 board target now carries its
  required console defaults, links the TM1629 runtime, honors board.json
  runtime capacities, builds from the current repo, flashes on
  `/dev/cu.usbserial-0001`, and answers direct `matrix.*` / `tm1629.raw.*`
  control smoke on hardware.
- The workshop implementation tranche is now closed on `main`: the delivery,
  inspection, workshop base-image, readability-language, and records cuts have
  all survived the local proof ladder plus repeated review cycles.
- The first detailed Friday workshop run spec is now checked in at
  `docs/roadmap/Frothy_Workshop_Run_Spec_2026-04-17.md`; it freezes the lesson
  arc, `Get Home` inspection puzzle, the shared `starter.frothy` scaffold, required helper
  surface, persistence teaching points, and rehearsal checklist.
- The v4 workshop-helper tranche is now landed on the maintained proto-board
  path: `esp32-devkit-v4-game-board` base now carries the generic workshop
  helpers plus `grid.*`, `joy.*`, and `knob.*`; board-configured Frothy
  capacities are driven from `board.json` on both host and ESP-IDF builds; the
  Friday workshop docs now describe the real v4 matrix/knob/joystick surface;
  and real-device proof on the mounted board froze the semantic joystick map to
  `left=JOY_1`, `click=JOY_2`, `down=JOY_3`, `up=JOY_4`, `right=JOY_6`, with
  `dangerous.wipe` restoring those base-owned pin slots on hardware.
- The first explicit evaluator-frame-stack tranche is now landed on `main`:
  `CALL`, `IF`, `WHILE`, `SEQ`, and required compound expression paths run
  through a bounded explicit frame stack instead of recursive evaluator entry,
  prompt-facing `record ...` forms now match the landed
  parser/evaluator/snapshot record surface, and the focused host proof slice
  now includes both the eval stack-budget tripwire and shell record coverage.
- The evaluator stack-overflow regression found by a simple ESP32 `boot` loop
  is therefore no longer defended only by compile-time frame-size hygiene; the
  explicit evaluator-frame-stack tranche is the maintained path, and the
  remaining bounded frame-arena ownership revisit is now deferred until
  multi-instance runtime work makes shared ownership matter.
- The ESP32 shell-path overflow on multiline `in` / `cond` / `case` definitions
  is fixed in the current tree by removing a 1KB rewrite buffer from
  `frothy_shell_run()`'s task stack, widening the stack-budget proof to cover
  parser/shell entry paths, and restoring the maintained ESP-IDF main-task
  stack setting to 8192 bytes in `targets/esp-idf/sdkconfig`.
- The host serial control path is now pruned of the legacy raw-`HELLO`
  discovery probe, the maintained macOS CLI transport uses the direct raw
  termios path that actually survives ESP32 prompt/control handoff, and the
  Frothy local connect build cache no longer collides with inherited Froth's
  stale `local-build` directory.
- The workshop release/install surface is now truthful in-repo: `README.md`,
  `docs/guide/Frothy_Workshop_Install_Quickstart.md`,
  `tools/package-release.sh`, and the VS Code docs all agree on the promised
  attendee path of released CLI assets, matching VSIX, and preflashed
  `esp32-devkit-v4-game-board` hardware.
- The workshop product shape is now simpler and single-sourced in-repo:
  `workshop/starter.frothy` is the sanctioned public starter scaffold,
  `frothy doctor` no longer treats source-build tools as attendee blockers,
  and the manual release workflow no longer promises an attendee firmware
  artifact that this tranche does not publish.
- Board-selected build input is now truthful again: host and ESP-IDF builds
  resolve board-owned extra C sources from the selected board declaration
  instead of carrying hardcoded TM1629 linkage in the global build lists, so
  the default v1 board path stays a plain base Frothy image unless a board
  explicitly declares more.
- The repo-checkout CLI selection path is now explicit again: `--target`
  means platform, `--board` means board, manifest projects ignore those flags
  with an explicit note instead of a hard error, legacy repo build/flash
  force-clean sticky target/board caches when selection flags are passed, and
  real-device proof now goes back through `frothy flash` rather than raw
  `idf.py` for the maintained repo-side flash path.
- The v4 workshop base-image seed regression from source comments is fixed:
  the C parser now treats backslash line comments as whitespace, matching the
  existing CLI source splitter, and the `esp32-devkit-v4-game-board` base image
  boots with the `puzzle.*` helpers present.
- The attendee-facing naming and recovery story is now explicit on the
  maintained Frothy path: Frothy owns the product/docs/editor/install identity,
  the default CLI home is `~/.frothy` with `FROTHY_HOME` override, Frothy now
  creates that home on demand instead of consulting legacy `~/.froth`,
  whole-file editor send attempts control `reset` before replay and marks the
  session degraded when the user explicitly chooses `Send Anyway` after reset is
  unavailable, and the control proof ladder now re-checks recovery on the real
  ESP32 path.
- The workshop editor/install surface is back in sync with the manifest:
  attendee docs now name the `NikolaiKozak.frothy` Marketplace listing, the
  editor-host smoke derives the extension id from `tools/vscode/package.json`,
  and the workshop ops proof checks that docs and manifest stay aligned.
- `esp32-devkit-v1` and `esp32-devkit-v4-game-board` remain accepted board
  models in the repo; the workshop promise is simply narrower and currently
  centered on the mounted preflashed v4 board.
- The workshop-operational slice is now concrete in-repo without widening the
  product surface: `README.md` points at one minimal front door,
  `docs/guide/Frothy_Workshop_Quick_Reference.md` keeps the in-room prompt and
  recovery path short, `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`
  freezes the promised clean-machine checklist,
  `boards/esp32-devkit-v4-game-board/WORKSHOP.md` holds the room-side kit and
  reflash card, and
  `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md` carries the
  checked-in rehearsal status note plus the focused
  `sh tools/frothy/proof.sh workshop-v4 <PORT>` real-device proof command.
- The remaining manual workshop gates stay explicit: separate clean-machine
  passes on the promised platforms and physical room pack-out are still exit
  steps, not work that prose can claim complete; the focused v4 mounted-board
  smoke is now recorded separately from any broader room rehearsal.
- The full publishability reset is now landed on `main`: stale
  proof artifacts are archived under `docs/archive/`, Frothy-facing
  naming/packaging are normalized, release packaging no longer needs Python,
  release CI uses the maintained VS Code host-smoke lane, firmware manifest
  parsing/ordering/validation and artifact path checks are centralized under
  the maintained Go surface, the default proof surface is back to `C` + `Go`
  + `Shell` with Node kept extension-local, and the retained Froth substrate
  boundary is explicit in code and docs.
  References:
  `docs/audit/Frothy_Repo_Audit_2026-04.md` and
  `docs/reference/Frothy_Retained_Substrate_Manifest.md`.
- The pre-thesis prune pass is active and tracked in
  `docs/audit/Frothy_Pre_Thesis_Prune_Plan_2026-05.md`: the first low-risk
  cut removes the tracked Mach-O helper binary, the unused Froth `board.froth`
  ESP-IDF embed path, and the generated guide PDF plus ReportLab renderer;
  Python hardware-proof holdouts are now replaced by Go-backed proof commands,
  and Frothy ADR-124 defers project-format renaming, keeps generated PDFs out
  of active source, and keeps Node as a VS Code-only exception.
- The first test-collapse cut is landed in the working tree: ignored ESP-IDF
  `build-*` caches no longer enter the embedded CLI SDK payload, local payload
  generation dropped from about 35 seconds / 62 MB to under 1 second / 130 KB
  on this checkout, and `make test` now runs the proportional edit-loop gate
  of fast Frothy CTest plus CLI unit tests instead of slow CMake/config smokes
  and the full host smoke-proof rehearsal. Current measured local shape:
  `make test` is about 7.5 seconds; `make test-all` is about 101 seconds.
- The Python hardware-proof holdouts are removed from the maintained tree:
  `proof_m10_smoke.sh` now delegates its ESP32 lane to the Go test-runner
  `proof-m10-device` command, `proof_v4_workshop_surface.sh` delegates to
  `proof-workshop-v4`, and the old M9/M10 Python serial proof scripts are
  deleted. The maintained M10 lane has passed on a real `esp32-devkit-v1`
  target at `/dev/cu.usbserial-0001`. The v1 board slot budget is now 192 so
  the maintained base FFI surface still leaves enough live user-slot headroom
  for the direct-control smoke; `make test-all` and
  `sh tools/frothy/proof.sh control /dev/cu.usbserial-0001` are green in this
  working tree. The flashed v1 image still has 0x96180 bytes free in the app
  partition; the current ELF reports `.dram0.data=115008` and
  `.dram0.bss=43104`.
- The front-door control docs now defer workshop-operational work for the
  current thesis-facing pass. The non-workshop validation closeout is green:
  `sh tools/frothy/proof_control_surface_docs.sh`, `make test-publishability`,
  and `sh tools/frothy/proof.sh control /dev/cu.usbserial-0001` have passed in
  this working tree. The VS Code host-smoke portion of `make
  test-publishability` needs normal desktop permissions rather than the sandbox
  used for most shell commands.
- The default non-workshop test gates no longer carry deferred workshop checks:
  `proof.sh host` stops before `workshop-docs`, and `make test-publishability`
  no longer runs the workshop export check. Those checks are still available
  explicitly through `make test-workshop`.
- The FFI-boundary prune is now the current C-runtime cut: Frothy ADR-125
  retires legacy board/project FFI exports, the POSIX and
  `esp32-devkit-v1` board tables are on `frothy_ffi_entry_t`, the public
  legacy installer and legacy project-FFI smoke fixture are removed, and the
  retained internal `froth_ffi.*` substrate audit is complete.
- The FFI installer rollback hardening cut follows that boundary prune:
  staged native values now have an explicit slot-transfer ownership contract,
  successful table replacement retires prior native bindings at the table
  commit point, application-phase install failure releases failed and
  unapplied staged natives after rolling back prior replacements, duplicate
  binding names fail before allocation, and focused C coverage now forces the
  failure path without adding hooks to the maintained runtime build. Validation
  passed with `make test-frothy`, the maintained `esp32-devkit-v1` ESP-IDF
  build, and the M10 proof on `/dev/cu.usbserial-0001`.
- The retained internal `froth_ffi.*` substrate audit found no live call sites
  for the old stack-oriented registration, lookup, push/pop, or binding macro
  surface. `src/froth_ffi.c`, `src/froth_ffi.h`, the dead
  `FROTH_FFI_MAX_TABLES` CMake define, and the project-manifest
  `ffi_max_tables` knob are removed from the maintained tree. Validation passed
  with `make test-frothy`, `go test ./...` from `tools/cli`, the maintained
  `esp32-devkit-v1` ESP-IDF build, and the M10 proof on
  `/dev/cu.usbserial-0001`.
- The source-inventory and compatibility-boundary prune is validated: host and
  ESP-IDF now share `cmake/frothy_runtime_sources.cmake`, the unused
  `src/compat/*` shims and `src/froth_link.h` dispatcher header are gone,
  transport exposes decode/send only, and the Go protocol package no longer
  carries stale attach/eval/info/error payload helpers from the old link path.
- The retained utility-substrate prune is validated: the unowned
  `froth_stack_*` helper API and `src/froth_stack.c` unit are gone while the
  VM-owned stack structs remain, and the two-function `froth_fmt` helper unit
  was folded into the then-live console helper. Validation passed with the host
  build, `make --no-print-directory test-frothy`, the maintained
  `esp32-devkit-v1` ESP-IDF build, and the M10 proof on
  `/dev/cu.usbserial-0001`.
- The ESP-IDF warning and dead-utility prune is validated in the current
  working tree: unused static helpers in the evaluator, shell, value, and
  snapshot codec are removed; the unused `froth_tbuf.[ch]` transient
  string-buffer substrate plus its CMake and project-manifest tuning knobs are
  gone; and both maintained ESP32 board ADC paths now use `adc_oneshot` instead
  of the deprecated ADC1 API. Validation passed with the host build,
  `make --no-print-directory test-frothy`, `go test ./...` from `tools/cli`,
  both `esp32-devkit-v1` and `esp32-devkit-v4-game-board` ESP-IDF builds, and
  the M10 proof on `/dev/cu.usbserial-0001`.
- The follow-on VM stack/config prune is validated in the current working
  tree: the unused inherited data/return/control stack storage and
  `src/froth_stack.h` are removed from `froth_vm_t`, the stale
  `FROTH_DS_CAPACITY`, `FROTH_RS_CAPACITY`, `FROTH_CS_CAPACITY`,
  `FROTH_MAX_PERM_SIZE`, `ds_depth`, and `rs_depth` knobs are gone, dead
  console/platform nonblocking-input wrappers are removed, and the old active
  `docs/perm-tutorial.md` stack-language tutorial is deleted. Validation
  passed with the host build, `make --no-print-directory test-frothy`,
  `go test ./...` from `tools/cli`, both `esp32-devkit-v1` and
  `esp32-devkit-v4-game-board` ESP-IDF builds, and the M10 proof on
  `/dev/cu.usbserial-0001`.
- The snapshot-ownership fold is validated in the current working tree:
  `src/froth_snapshot.[ch]` is deleted, snapshot header construction/parsing
  and A/B slot selection now live beside save/restore/wipe in
  `src/frothy_snapshot.[ch]`, the retained-substrate manifest no longer names
  snapshot as a separate live Froth unit, and the tracked source inventory is
  down to 38 `.c` files and 27 `.h` files. Validation passed with
  `git diff --check`, the host build, `make --no-print-directory test-frothy`,
  `go test ./...` from `tools/cli`, both maintained ESP-IDF board builds, and
  the M10 proof on `/dev/cu.usbserial-0001`.
- The transport-ownership fold is validated in the current working tree:
  `src/froth_transport.[ch]` is deleted, the COBS/header/frame helpers now
  live privately in `src/frothy_control.c`, and the CLI protocol mirror points
  at the Frothy control owner instead of a retained transport unit. Validation
  passed with `git diff --check`, the host build,
  `make --no-print-directory test-frothy`, `go test ./...` from `tools/cli`,
  both maintained ESP-IDF board builds, and the M10 proof on
  `/dev/cu.usbserial-0001`.
- The console-helper ownership fold is validated in the current working tree:
  `src/froth_console.[ch]` is deleted and the remaining board-facing emit,
  poll, and number-format helpers now live on the maintained
  `src/frothy_ffi.[ch]` surface used by board bindings. Validation passed with
  `git diff --check`, the host build, `make --no-print-directory test-frothy`,
  `go test ./...` from `tools/cli`, both maintained ESP-IDF board builds, and
  the M10 proof on `/dev/cu.usbserial-0001`.
- The retained VM/lifecycle audit is validated in the current working tree:
  write-only inherited VM fields are deleted, repeated test/bench reset pokes
  now use explicit global VM reset and boot-complete helpers, and the kept heap,
  cellspace, slot-table, and CRC units are documented as intentional retained
  substrate; overlay reset also preserves later non-overlay slots instead of
  relying on allocation ordering. Validation passed with `git diff --check`,
  the host build,
  `make --no-print-directory test-frothy`, both maintained ESP-IDF board
  builds, and the M10 proof on `/dev/cu.usbserial-0001`.
- The Frothy test-speed split is active in the current working tree:
  Frothy ADR-126 narrows `make test-frothy` to the fast Frothy CTest lane,
  adds explicit `make test-frothy-slow`, `make test-frothy-proofs`, and
  `make test-frothy-full` targets, and keeps `make test-all` as the extended
  local C/Go/shell gate. CI now runs the slower Frothy lanes as separate jobs,
  and the previous disguised `test-frothy` proof bundle is still available as
  `test-frothy-full`; existing scripts that used `make test-frothy` for full
  host coverage should move to `make test-frothy-full`. Validation passed with
  `git diff --check`, `make --no-print-directory test-list`,
  `make --no-print-directory test-frothy`,
  `make --no-print-directory test-frothy-slow`,
  `make --no-print-directory test-frothy-proofs`,
  `make --no-print-directory test-frothy-full`,
  `make --no-print-directory test`, `make --no-print-directory test-all`,
  `go test ./...` from `tools/cli`, and the M10 proof on
  `/dev/cu.usbserial-0001`.
- The `test-all` CLI portion is deduplicated in the current working tree:
  Frothy ADR-127 keeps `make test-cli-local` as the focused local-runtime CLI
  integration lane, replacing the old no-op-tag rerun of ordinary `cmd` unit
  tests; it also makes `make test-integration` run only the remaining
  build/project integration cases, with a source-list drift check so new
  `TestIntegration...` cases cannot silently fall out of both lanes.
  Warm-cache `make test-all` is now about 54 seconds on this checkout, with
  the host proof bundle still the dominant cost. Validation passed with
  `git diff --check`, `make --no-print-directory test-list`,
  `make --no-print-directory -C tools/cli check-integration-tests`,
  `make --no-print-directory test-cli-local`,
  `make --no-print-directory test-integration`,
  `make --no-print-directory test-all`, `go test ./...` from `tools/cli`,
  focused Claude review with no actionable findings, and the M10 proof on
  `/dev/cu.usbserial-0001`.

## Remaining Gates

- Pre-thesis publishability prune is the active non-workshop gate: the
  project-format, generated-PDF, and VS Code/Node policy decisions are captured
  by Frothy ADR-124; the old Python hardware-proof holdouts are gone; and the
  legacy board/project FFI export path is retired under Frothy ADR-125. With
  the retained substrate audit closed, the Frothy host test lanes split, and
  the CLI integration lanes deduplicated, the next useful cleanup line is the
  remaining host proof-script bundle.
- Workshop-operational closeout is explicitly deferred behind the thesis-facing
  prune:
  clean-machine validation and room-side hardware/recovery prep still need to
  be executed, and one complete recorded rehearsal pass still needs to be
  captured; the focused v4 workshop-board hardware smoke is now recorded.
- The evaluator frame-arena ownership revisit is deferred: the explicit
  evaluator-frame-stack tranche is landed, and the remaining shared-ownership
  question does not block the maintained single-runtime path until Frothy
  intentionally grows multiple live runtime instances or another re-entrant
  evaluator owner.
- Workspace/image flow remains intentionally deferred and single-sourced in
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- See `TIMELINE.md` for the live movable queue and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` for the
  rationale behind that order.
