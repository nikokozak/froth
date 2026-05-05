# Frothy Pre-Thesis Prune Plan 2026-05

Status: active cleanup plan
Date: 2026-05-03
Authority:
`docs/spec/Frothy_Language_Spec_v0_1.md`,
Frothy ADR-109,
Frothy ADR-120,
`docs/audit/Frothy_Repo_Audit_2026-04.md`,
and the roadmap current-state block

## Purpose

This plan is the public-readiness cleanup path before Frothy is introduced as
part of the thesis.

The target shape is a small source tree that a reader can understand without
learning the old Froth stack language, daemon era, or packaging history first.
The maintained core dependency budget should be `C` plus `Go`. Small POSIX
shell wrappers may stay only as command glue. Python should leave the
maintained tree.

## Current Inventory

Tracked source after the first immediate prune slice, counting files still
present in the working tree:

- `C` and headers: 36 `.c`, 25 `.h`
- Go: 80 `.go`
- Frothy source and parser fixtures: 39 `.frothy`, 29 `.ir`
- Shell glue: 25 `.sh`
- VS Code extension: 8 `.ts`, 9 `.js`
- Python holdouts: none
- Generated/reference artifact holdout: none in the active guide path

Immediate issue already cut in this pass:

- tracked Mach-O helper binary: `tools/cli/frothy-m10-device-smoke`
- old Froth board-library embed path:
  `boards/*/lib/board.froth`, `cmake/embed_froth.cmake`, and the
  `FROTH_HAS_BOARD_*` CMake target path
- generated guide PDF plus its ReportLab Python renderer:
  `docs/guide/Frothy_From_The_Ground_Up.pdf` and
  `tools/docs/render_frothy_guide_pdf.py`
- ignored ESP-IDF build caches accidentally embedded into the CLI SDK payload;
  `tools/cli/internal/sdk/cmd/generate` now rejects `build-*` directories,
  reducing local SDK payload generation from about 35 seconds / 62 MB to under
  1 second / 130 KB on this checkout
- full host smoke proofs moved out of the default `make test` edit-loop gate;
  slow CMake/config smokes are labeled `frothy_slow`; `make test` is fast
  CTest plus CLI unit tests; and Frothy ADR-126 now splits
  `make test-frothy` from `make test-frothy-slow`,
  `make test-frothy-proofs`, and `make test-frothy-full`
- legacy board/project FFI exports retired under Frothy ADR-125; POSIX and
  `esp32-devkit-v1` now use `frothy_ffi_entry_t`, the public legacy installer
  is gone, and the legacy project-FFI fixture has been deleted
- dead C utility substrate removed after retained-source audit:
  `src/froth_ffi.[ch]`, `src/froth_stack.[ch]`, `src/froth_fmt.[ch]`, and
  `src/froth_tbuf.[ch]` no longer ship on the maintained path
- stale transient-buffer CMake and project-manifest tuning knobs removed:
  `FROTH_TBUF_SIZE`, `FROTH_TDESC_MAX`, `tbuf_size`, and `tdesc_max`
- stale inherited stack/permutation tuning knobs removed:
  `FROTH_DS_CAPACITY`, `FROTH_RS_CAPACITY`, `FROTH_CS_CAPACITY`,
  `FROTH_MAX_PERM_SIZE`, `ds_depth`, and `rs_depth`
- maintained ESP32 board ADC bindings moved from the deprecated ADC1 driver API
  to the ESP-IDF `adc_oneshot` API
- stale active stack-language tutorial removed:
  `docs/perm-tutorial.md`
- old retained snapshot header/slot-selection unit folded into the Frothy-owned
  snapshot owner: `src/froth_snapshot.[ch]` no longer ships beside
  `src/frothy_snapshot.[ch]`
- old retained transport unit folded into the Frothy-owned control owner:
  COBS/header/frame handling now lives privately in `src/frothy_control.c`, and
  `src/froth_transport.[ch]` no longer ships as a separate public C surface
- old retained console helper folded into the Frothy-owned FFI owner:
  board-facing emit, poll, and number-format helpers now live in
  `src/frothy_ffi.[ch]`, and `src/froth_console.[ch]` no longer ships as a
  separate retained C surface
- retained VM/lifecycle substrate tightened without a broad rename:
  write-only inherited VM fields are gone, repeated test/bench VM reset pokes
  now use the explicit global VM reset/boot-complete helpers, slot reset has an
  all-slot lifecycle owner for fresh VM starts, overlay reset no longer depends
  on base slots being allocated before overlay slots, and the kept
  heap/cellspace/slot-table/CRC units are documented as live substrate rather
  than accidental leftovers

Remaining cleanup pressure:

- M9/M10/v4 hardware proof orchestration now sits behind Go test-runner
  commands, with shell wrappers kept as thin entrypoints.
- The project format intentionally keeps `froth.toml`, `.froth-build`, and
  `src/main.froth` under Frothy ADR-124.
- The VS Code extension keeps Node/TypeScript as an explicit extension-local
  exception under Frothy ADR-124.
- Retained `src/froth_*` substrate is documented but still interleaved with
  product runtime code; after Frothy ADR-125, the internal `froth_ffi.*`
  substrate audit found no live owner, and the follow-on source inventory
  removed the final `src/compat/*` C shims and unused inherited link-dispatch
  header. The utility cuts have now removed the unowned `froth_stack_*` helper
  API, the emptyable `src/froth_stack.c` unit, the unused VM stack storage
  structs, the two-function `froth_fmt` helper unit, and the unused
  `froth_tbuf` transient string-buffer substrate. The snapshot cut folds the
  old retained `froth_snapshot.[ch]` header and A/B slot-selection plumbing
  into `src/frothy_snapshot.[ch]`, leaving snapshot save/restore/wipe under one
  Frothy owner. The transport cut folds the old retained
  `froth_transport.[ch]` COBS/header/frame helpers into `src/frothy_control.c`,
  leaving the control wire format stable but removing another public retained
  substrate unit. The console cut folds the old retained `froth_console.[ch]`
  emit/poll/format helpers into `src/frothy_ffi.[ch]`, where their only live
  board FFI callers already anchor the dependency. The VM/lifecycle audit then
  removes write-only inherited VM fields and leaves the remaining `froth_vm`,
  `froth_heap`, `froth_cellspace`, `froth_slot_table`, and `froth_crc32` units
  as explicitly documented retained substrate.

## Authority Tensions

Frothy ADR-120 explicitly says that its identity tranche did not rename
`froth.toml`, `.froth-build`, or source extensions. Frothy ADR-124 now defers
that rename for the thesis snapshot instead of treating the current names as
quiet drift.

The April publishability audit allowed temporary Python only in hardware-only
lanes. The current thesis-clean target is stricter: keep the maintained proof
surface on Go plus shell wrappers, and do not add Python back as proof glue.

## Course Of Action

### 1. Freeze The Public Surface

Decide what ships in this repository for the thesis snapshot:

- core language/runtime: keep
- CLI: keep
- ESP32 target and accepted boards: keep
- VS Code extension: keep extension-local with Node quarantined under Frothy
  ADR-124
- historical archives and generated PDFs: keep generated PDFs outside active
  source under Frothy ADR-124

Exit proof:

- `make test-list`
- `rg -n 'Python|Node|TypeScript|froth.toml|\\.froth-build|\\.froth\\b' README.md docs/guide tools/cli tools/vscode`

### 2. Remove Dead Froth Surfaces First

Cut artifacts with no runtime owner before touching semantics:

- generated binaries and captures under active source paths
- old Froth board asset embedding
- stale daemon-era docs outside archive/reference space
- stale `.froth` examples that are not part of the accepted project-format
  compatibility story

Exit proof:

- `make test`
- `make test-all`
- `! rg -n 'froth_embed_board_assets|board\\.froth|FROTH_HAS_BOARD_LIB|FROTH_HAS_BOARD_PINS|frothy-m10-device-smoke' --glob '!docs/audit/**' --glob '!PROGRESS.md'`

### 3. Replace Python Hardware Proofs With Go

Move serial proof orchestration into `tools/cli/cmd/test-runner` or a narrow
Go helper under `tools/cli/cmd/`.

Replaced in the current tranche:

- `tools/frothy/proof_m9_esp32_ffi_smoke.py`
- `tools/frothy/proof_m10_esp32_smoke.py`
- the inline Python in `tools/frothy/proof_v4_workshop_surface.sh`

Preferred shape:

- use the existing Go direct-control session for board assertions
- use `frothy flash` for repo-side device setup
- reserve raw monitor handling only for boot/safe-boot checks that truly need
  it, and implement that in Go without adding a new third-party dependency

Exit proof:

- `! git ls-files '*.py' | rg .`
- `! rg -n 'python3|/usr/bin/env python|<<.PY' tools tests .github`
- `sh tools/frothy/proof.sh control <PORT>`
- `sh tools/frothy/proof.sh m10 <PORT>`
- `sh tools/frothy/proof.sh workshop-v4 <PORT>` when the v4 board is the target

### 3a. Keep Test Gates Proportional

Current measured local shape after the Frothy test-speed split on this
checkout. These are warm-cache validation observations; cold Go or CMake
caches can add time.

- `make test-frothy`: under half a second after the host profile is current
- `make test-frothy-slow`: about 8 seconds on this checkout
- `make test-frothy-proofs`: about 45 seconds on this checkout
- `make test-all`: about 84 seconds on this checkout in the warm-cache split
  validation, with CLI integration still around 28 seconds; prior 101-second
  `test-all` and 34-second CLI integration observations were from an earlier
  mixed-cache pass

Policy:

- `make test` should stay the edit-loop gate: fast host CTest plus CLI unit
  tests
- `make test-frothy` should stay the tight Frothy runtime/language lane:
  fast host CTests only
- `make test-frothy-full` should preserve the previous Frothy-specific
  full-host breadth
- `make test-all` should stay the extended local gate: fast gate plus slow
  CTest, host smoke proofs, local-runtime tests, and integration tests
- CI should run the slow Frothy CTest and host-proof lanes as separate jobs so
  the coverage is explicit instead of local-only
- deferred workshop docs/export checks should stay out of the default
  non-workshop gates and remain explicit through `make test-workshop`
- real-device proofs should remain explicit and named, not hidden inside local
  gates
- new smoke proofs must justify why focused C or Go unit coverage is not enough

Next cleanup candidates:

- split CLI integration tests so build-system integration can run separately
  from ordinary command behavior
- move long host prompt/proof scripts toward focused Go tests where possible
  rather than growing shell transcripts
- keep workshop rehearsal and docs/export proofs out of default local gates

### 4. Defer Project-Format Naming

Frothy ADR-124 closes this as a pre-thesis blocker.

Policy:

- keep `froth.toml`, `.froth-build`, `.froth`, and `src/main.froth`
  authoritative for the thesis snapshot
- do not introduce `frothy.toml`, `.frothy-build`, or `.frothy` source
  extensions in this tranche
- reopen project-format naming only through a later identity ADR, especially if
  the broader product identity returns to Froth

Exit proof:

- `make test-all`
- `frothy new /tmp/frothy-project-smoke`
- `rg -n 'Frothy ADR-124|froth\\.toml|\\.froth-build|src/main\\.froth' README.md docs/guide tools/cli tools/vscode docs/adr docs/roadmap`

### 5. Tighten The C Runtime Boundary

Do not blind-rename retained `froth_*` files. Instead:

- keep legacy board/project FFI exports deleted now that active board/project
  FFI is fully on `frothy_ffi_entry_t`
- keep retired `froth_ffi.*` substrate out of the maintained build now that
  the audit found no live runtime owner
- keep `src/compat/*` deleted now that no retained substrate requires it
- keep one CMake source list as the shared host/ESP-IDF runtime inventory
- keep the removed inherited VM stack storage and board stack-depth knobs out
  of the maintained tree; Frothy execution now uses the Frothy evaluator frame
  stack rather than the old data/return/control stack arrays
- keep board-facing emit/poll/format helpers in the Frothy FFI owner instead of
  a separate unowned console source/header pair
- keep the deleted `froth_tbuf` transient-buffer path out of the maintained
  tree; Frothy runtime text storage now uses the Frothy object allocator path
- keep ESP32 board ADC reads on `adc_oneshot` rather than the deprecated ADC1
  driver API
- keep `froth_snapshot.[ch]` deleted now that its remaining header and slot
  selection behavior lives in the Frothy snapshot owner
- keep `froth_transport.[ch]` deleted now that its remaining COBS/header/frame
  behavior lives privately in the Frothy control owner
- keep `froth_console.[ch]` deleted now that its remaining board-facing helper
  behavior lives in the Frothy FFI owner
- keep the VM reset/boot-complete lifecycle explicit and keep write-only
  inherited VM fields deleted
- keep `froth_heap`, `froth_cellspace`, `froth_slot_table`, and `froth_crc32`
  as named retained substrate while they have multiple live runtime/proof
  owners
- move retained substrate into an explicit directory only if it reduces reader
  confusion without hiding ownership

Exit proof:

- `cmake -S . -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure -L frothy`
- `./tools/cli/frothy-cli build --target esp-idf --board esp32-devkit-v1`
- `./tools/cli/frothy-cli build --target esp-idf --board esp32-devkit-v4-game-board`
- `sh tools/frothy/proof.sh control <PORT>`

### 6. Collapse Docs To One Reader Path

For a public thesis snapshot:

- keep `README.md` short and product-facing
- keep the accepted spec and active ADRs
- keep one guide path
- keep generated PDFs out of active source; if a PDF is needed, produce it as a
  release or external publication artifact
- keep old Froth references only in `docs/reference/` or `docs/archive/`

Exit proof:

- `sh tools/frothy/proof_control_surface_docs.sh`
- `rg -n 'Froth|froth' README.md docs/guide docs/spec docs/adr/1*.md docs/reference`

## Cut Line For This Week

If time is tight, do these before the thesis snapshot:

1. keep `make test-all` plus one real-device `control` proof on
   `esp32-devkit-v1` green
2. keep generated guide PDFs outside active source under Frothy ADR-124
3. keep VS Code/Node explicitly extension-local under Frothy ADR-124

The Go-backed M10 hardware proof migration has now passed on a real
`esp32-devkit-v1` through `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001`.
The closeout gate also passed with `make test-all` and
`sh tools/frothy/proof.sh control /dev/cu.usbserial-0001`.

Do not start a broad internal C symbol rename this week. It is high churn, it
does not materially improve thesis readability, and it risks the maintained
hardware path.
