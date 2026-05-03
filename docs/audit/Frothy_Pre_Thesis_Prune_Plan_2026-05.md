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

- `C` and headers: 46 `.c`, 33 `.h`
- Go: 79 `.go`
- Frothy source and parser fixtures: 39 `.frothy`, 29 `.ir`
- Shell glue: 25 `.sh`
- VS Code extension: 8 `.ts`, 9 `.js`
- Python holdouts: 2 `.py`
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
  slow CMake/config smokes are labeled `frothy_slow`; `make test` is now
  fast CTest plus CLI unit tests, while `make test-all` and `make test-frothy`
  still carry slow CTest and the host proof rehearsal lane

Remaining cleanup pressure:

- Python still owns M9/M10 hardware proof logic.
- `tools/frothy/proof_v4_workshop_surface.sh` embeds Python inline.
- The project format still teaches `froth.toml`, `.froth-build`, and
  `src/main.froth`.
- The VS Code extension keeps Node/TypeScript in the repo.
- Retained `src/froth_*` substrate is documented but still interleaved with
  product runtime code and compatibility shims.

## Authority Tensions

Frothy ADR-120 explicitly says that its identity tranche did not rename
`froth.toml`, `.froth-build`, or source extensions. The current cleanup goal
now points at revisiting those names. That should be done with a new Frothy ADR
instead of a quiet broad rename.

The April publishability audit allowed temporary Python only in hardware-only
lanes. The current thesis-clean target is stricter: replace those paths with
Go or remove them from the maintained proof surface.

## Course Of Action

### 1. Freeze The Public Surface

Decide what ships in this repository for the thesis snapshot:

- core language/runtime: keep
- CLI: keep
- ESP32 target and accepted boards: keep
- VS Code extension: either keep extension-local with Node quarantined, or move
  it to a sibling repo/package before claiming a `C` plus `Go` source tree
- historical archives and generated PDFs: archive outside active source or
  drop from the public snapshot

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

Replace:

- `tools/frothy/proof_m9_esp32_ffi_smoke.py`
- `tools/frothy/proof_m10_esp32_smoke.py`
- the inline Python in `tools/frothy/proof_v4_workshop_surface.sh`

Preferred shape:

- use the existing Go direct-control session for most board assertions
- use `frothy build` and `frothy flash` for repo-side device setup
- reserve raw monitor handling only for boot/safe-boot checks that truly need
  it, and implement that in Go without adding a new third-party dependency

Exit proof:

- `! git ls-files '*.py' | rg .`
- `! rg -n 'python3|/usr/bin/env python|<<.PY' tools tests .github`
- `sh tools/frothy/proof.sh control <PORT>`
- `sh tools/frothy/proof.sh workshop-v4 <PORT>` when the v4 board is the target

### 3a. Keep Test Gates Proportional

Current measured local shape after the first test-collapse cut:

- `make test`: about 7.5 seconds on this checkout
- `make test-all`: about 101 seconds on this checkout
- remaining extended costs: slow CTest around 14 seconds, host smoke proofs
  around 46 seconds, CLI integration around 34 seconds

Policy:

- `make test` should stay the edit-loop gate: fast host CTest plus CLI unit
  tests
- `make test-all` should stay the extended local gate: fast gate plus slow
  CTest, host smoke proofs, local-runtime tests, and integration tests
- real-device proofs should remain explicit and named, not hidden inside local
  gates
- new smoke proofs must justify why focused C or Go unit coverage is not enough

Next cleanup candidates:

- split CLI integration tests so build-system integration can run separately
  from ordinary command behavior
- move long host prompt/proof scripts toward focused Go tests where possible
  rather than growing shell transcripts
- keep workshop rehearsal proofs out of default local gates

### 4. Decide Project-Format Naming

Write a new Frothy ADR before changing this surface.

Recommended direction:

- keep backward reads of `froth.toml` for one transition window
- make new projects emit `frothy.toml`
- make new source files emit `src/main.frothy`
- move derived build output from `.froth-build/` to `.frothy-build/`
- keep compatibility warnings explicit and temporary

Exit proof:

- `make test-all`
- `frothy new /tmp/frothy-project-smoke`
- `rg -n 'src/main\\.froth|froth\\.toml|\\.froth-build' README.md docs/guide tools/cli tools/vscode`

### 5. Tighten The C Runtime Boundary

Do not blind-rename retained `froth_*` files. Instead:

- delete legacy FFI exports after board/project FFI are fully on
  `frothy_ffi_entry_t`
- remove `src/frothy_ffi_legacy.h`
- remove `src/compat/*` once no retained substrate requires it
- make one CMake source list the shared host/ESP-IDF runtime inventory
- move retained substrate into an explicit directory only if it reduces reader
  confusion without hiding ownership

Exit proof:

- `cmake -S . -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure -L frothy`
- `sh tools/frothy/proof.sh control <PORT>`

### 6. Collapse Docs To One Reader Path

For a public thesis snapshot:

- keep `README.md` short and product-facing
- keep the accepted spec and active ADRs
- keep one guide path
- remove generated PDF from active source unless PDF generation is moved to Go
  or an external release process
- keep old Froth references only in `docs/reference/` or `docs/archive/`

Exit proof:

- `sh tools/frothy/proof_control_surface_docs.sh`
- `rg -n 'Froth|froth' README.md docs/guide docs/spec docs/adr/1*.md docs/reference`

## Cut Line For This Week

If time is tight, do these before the thesis snapshot:

1. finish the Python removal from hardware proofs
2. keep generated guide PDFs outside the active repo unless PDF generation is
   rebuilt in Go or moved to release-only automation
3. write the project-format ADR even if the rename lands after the thesis
4. keep VS Code either explicitly extension-local or out of the core public
   repo claim
5. run `make test-all` plus one real-device `control` proof on
   `esp32-devkit-v1`

Do not start a broad internal C symbol rename this week. It is high churn, it
does not materially improve thesis readability, and it risks the maintained
hardware path.
