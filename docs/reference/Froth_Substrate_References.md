# Froth Substrate References

The original Froth repo at `/Users/niko/Developer/Froth` is reference material
only.

Use the following files when Frothy intentionally reuses substrate behavior:

## Core Reuse Targets

- `docs/spec/Froth_Interactive_Development_v0_5.md`
- `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md`
- `src/froth_slot_table.h`
- `src/froth_slot_table.c`
- `src/froth_cellspace.h`

The old Froth snapshot storage/header implementation is historical reference
only. The maintained Frothy tree now owns that live plumbing in
`src/frothy_snapshot.c` and `src/frothy_snapshot.h`.

For the current retained build surface, also see:

- `docs/reference/Frothy_Retained_Substrate_Manifest.md`

## Limited Background Only

- `PROGRESS.md`
- `TIMELINE.md`

Read those only for sequencing context if needed. They are not active Frothy
control docs.

## Not Authoritative For Frothy

Do not treat these as active Frothy policy:

- Froth repo-local startup guidance and compatibility pointer files
- Froth language roadmap and milestone plan
- Froth stack-visible language semantics
- Froth ADR-054 / ADR-055 / ADR-056 sequencing as a Frothy implementation plan
