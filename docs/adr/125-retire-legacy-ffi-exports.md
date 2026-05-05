# Frothy ADR-125: Retire Legacy FFI Exports

**Date**: 2026-05-04
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 8.1, 9, 10
**Roadmap milestone(s)**: pre-thesis publishability prune
**Depends on**: Frothy ADR-108
**Inherited Froth references**: `src/froth_ffi.h`, `src/froth_ffi.c`

## Context

Frothy ADR-108 accepted a temporary bootstrap shape: present a Frothy
value-oriented foreign call surface while reusing enough Froth FFI substrate to
prove hardware behavior quickly.

That bootstrap path is now too broad for a public thesis snapshot. Before this
decision, board and project authors could still expose `froth_ffi_entry_t`
tables through `froth_board_bindings` or `froth_project_bindings`, and the
runtime still contained project and board fallback dispatch for that
stack-oriented ABI.

The retained Froth substrate remains useful internally, but it should no
longer be a public authoring surface for Frothy boards or projects.

## Options Considered

### Option A: Keep legacy FFI exports indefinitely

Continue accepting both `frothy_ffi_entry_t` and `froth_ffi_entry_t` exports.

Trade-offs:

- Pro: avoids short-term porting work.
- Con: keeps two board/project ABIs in the public mental model.
- Con: encourages new code to copy the wrong surface.
- Con: keeps stack-visible implementation details alive after Frothy has moved
  to value-oriented calls.

### Option B: Retire legacy exports in tranches

Make `frothy_ffi_entry_t` the only maintained board/project ABI, port live
boards and tests, then remove fallback dispatch and public compatibility
installers once no active source path needs them.

Trade-offs:

- Pro: keeps the public ABI small and Frothy-owned.
- Pro: allows retained internal substrate to shrink only after each dependency
  is proved gone.
- Con: requires short-term porting work in the board surfaces.

### Option C: Redesign the native ABI before pruning

Replace both the inherited stack ABI and current Frothy table ABI with a new
native API first.

Trade-offs:

- Pro: could produce a cleaner final shape.
- Con: adds design risk and churn before the public snapshot.
- Con: delays deleting known compatibility debt.

## Decision

**Option B.**

The maintained board/project FFI export is `frothy_ffi_entry_t`.

Rules:

- New board and project FFI code must export `frothy_board_bindings` or
  `frothy_project_bindings`.
- New board and project FFI code must not export `froth_board_bindings`,
  `froth_project_bindings`, or require `FROTHY_*_FFI_USE_LEGACY_EXPORT`.
- `frothy_ffi_install_table(...)` is the maintained installer.
- `frothy_ffi_install_binding_table(...)` is not a maintained public surface and
  must be removed once active tests, benchmarks, boards, and project examples no
  longer use it.
- Retained `froth_ffi.*` substrate may remain only where an internal runtime
  owner still needs it, such as retained snapshot primitives or other explicit
  substrate code.

Implementation note, 2026-05-05: the follow-on internal substrate audit found
no live owner for `src/froth_ffi.c`, `src/froth_ffi.h`, `FROTH_FFI_MAX_TABLES`,
or the project-manifest `ffi_max_tables` build knob. Those paths are no longer
part of the maintained Frothy tree.

## Consequences

- The public FFI story becomes one value-oriented Frothy ABI.
- Board/project fallback paths can be deleted instead of explained.
- Retained Froth FFI code can be audited as internal substrate rather than as
  user-facing extension policy.
- Public docs and scaffolds must stop teaching legacy FFI exports.

## References

- `docs/adr/108-frothy-ffi-boundary.md`
- `docs/reference/Frothy_Retained_Substrate_Manifest.md`
- `src/frothy_ffi.h`
- `src/frothy_ffi.c`
