# Frothy Retained Substrate Manifest

Status: active reference
Date: 2026-05-05
Authority: `docs/reference/Froth_Substrate_References.md`, Frothy ADR-125, `cmake/frothy_runtime_sources.cmake`, `CMakeLists.txt`, `targets/esp-idf/main/CMakeLists.txt`

This manifest names the retained Froth substrate that still ships in the
maintained Frothy tree.

It exists to keep the boundary explicit:

- retained `froth_*` units are still live substrate, not accidental leftovers
- Frothy-owned runtime code remains the maintained product surface
- retired legacy board/project authoring surfaces stay out of the active path

## Retained Froth Substrate

These retained source files still build on maintained Frothy paths because the
current runtime still reuses them directly:

- `src/froth_vm.c`
- `src/froth_heap.c`
- `src/froth_cellspace.c`
- `src/froth_slot_table.c`
- `src/froth_crc32.c`

The corresponding retained public headers are:

- `src/froth_cellspace.h`
- `src/froth_crc32.h`
- `src/froth_heap.h`
- `src/froth_slot_table.h`
- `src/froth_types.h`
- `src/froth_vm.h`

Current retained-unit notes:

- `froth_vm` owns the single global VM image, its static heap/cellspace backing
  storage, and the explicit reset/boot-complete lifecycle. The write-only
  inherited `thrown`, `last_error_slot`, `trampoline_depth`, and `mark_offset`
  fields are gone.
- `froth_heap` remains the byte/cell arena used by slot names and quote payload
  storage. Its high-water helpers are retained for memory proofs.
- `froth_cellspace` remains the mutable cell arena used by `cells(...)`,
  snapshots, and base-image reset. Its high-water helpers are retained for
  memory proofs.
- `froth_slot_table` remains the single global slot registry for base and
  overlay bindings. It now has an explicit all-slot reset used by VM reset, and
  overlay reset remains the user-image wipe path without relying on all base
  slots being allocated before all overlay slots.
- `froth_crc32` remains a tiny shared checksum unit used by both snapshot
  storage and the control frame wire format.

## Frothy-Owned Runtime Surface

These are Frothy-owned runtime or board-facing sources on the maintained path:

- `src/frothy_ffi.c`
- `src/frothy_tm1629.c`
- `boards/<board>/ffi.c`
- the `src/frothy_*.c` language/runtime units listed in the host and ESP-IDF
  build targets

New board and project FFI code must use the maintained `frothy_ffi_entry_t`
path declared in `src/frothy_ffi.h`.
The small board-facing emit, poll, and number-format helpers that previously
lived in `src/froth_console.[ch]` now live in `src/frothy_ffi.[ch]` because
their only maintained callers are board FFI bindings.

Snapshot ownership now lives in `src/frothy_snapshot.c` /
`src/frothy_snapshot.h`. The old retained `froth_snapshot.[ch]` unit was folded
there because the only live behavior left in it was snapshot header
construction, header parsing, and A/B slot selection for the Frothy-owned
save/restore/wipe path.

## Compatibility Layer

No source-level C compatibility shims remain on the maintained build path.
The prior `src/compat/*` shims and the unused `src/froth_link.h` dispatcher
header were removed after source inventory found no retained call sites.
The prior retained `src/froth_transport.[ch]` unit is also gone; the only live
wire behavior left there was COBS/header/frame handling for the Frothy control
session, so that behavior now lives privately in `src/frothy_control.c`.

Per Frothy ADR-125, active board and project FFI code must not export
`froth_board_bindings` or `froth_project_bindings`, and must not call a legacy
binding-table installer.

## Current Legacy Holdouts

No active board or project legacy FFI exports remain in the maintained tree.
The retained internal `src/froth_ffi.c` / `src/froth_ffi.h` substrate was
audited, found to have no live runtime owner, and removed from host, ESP-IDF,
and project-manifest build configuration. The former compatibility sources are
also removed from host and ESP-IDF source inventories.
