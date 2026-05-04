# Frothy ADR-124: Pre-Thesis Project Format And Dependency Policy

**Date**: 2026-05-04
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1 and 8; Appendix B
**Roadmap milestone(s)**: pre-thesis publishability prune
**Related ADRs**: `docs/adr/109-repo-control-surface-and-proof-path.md`, `docs/adr/111-vscode-extension-owned-control-session.md`, `docs/adr/113-manifest-owned-project-target-selection.md`, `docs/adr/120-cli-command-and-home-identity.md`, `docs/adr/122-public-workshop-release-surface.md`

## Context

The pre-thesis prune surfaced three open policy questions that were blocking
forward motion:

- whether to rename project-format paths such as `froth.toml`,
  `.froth-build`, `.froth`, and `src/main.froth`
- whether generated guide PDFs belong in active source
- whether Node remains acceptable while the repo is otherwise being tightened
  to the maintained `C` + `Go` + shell proof surface

Frothy ADR-120 intentionally changed the user-facing CLI/install identity while
leaving the project format alone. Reopening the project format now would create
large churn for little thesis value. The longer-term identity direction may
return to the Froth name, so renaming project files from `froth*` to `frothy*`
now would likely create another reversal later.

## Options Considered

### Option A: Rename the project format before the thesis snapshot

Change new projects to emit `frothy.toml`, `src/main.frothy`, and
`.frothy-build/`, while keeping temporary compatibility reads of the current
format.

Trade-offs:

- Pro: superficially aligns project files with the current Frothy CLI name.
- Con: creates broad docs, test, CLI, editor, and user-workspace churn.
- Con: weakens the likely future path back to the Froth name.
- Con: does not materially improve the public thesis codebase.

### Option B: Defer project-format rename and keep the current Froth-format names authoritative

Keep `froth.toml`, `.froth-build`, `.froth`, and `src/main.froth` as the
authoritative project-format surface for the thesis snapshot. Reopen naming
only through a later identity ADR.

Trade-offs:

- Pro: avoids churn before publication.
- Pro: keeps current workspaces and docs stable.
- Pro: matches the likely longer-term return to the Froth name.
- Con: the repo temporarily has Frothy CLI/install identity beside Froth-format
  project names.

### Option C: Remove the VS Code extension to eliminate Node

Delete the in-tree VS Code extension and keep the repository to `C`, `Go`, and
shell only.

Trade-offs:

- Pro: simplest dependency story.
- Con: removes a real shipped/editor surface.
- Con: contradicts Frothy ADR-111 and current release packaging.

## Decision

**Option B.**

For the pre-thesis snapshot:

- Defer project-format renaming.
- Keep `froth.toml`, `.froth-build`, `.froth`, and `src/main.froth` as the
  maintained project-format names.
- Do not introduce `frothy.toml`, `.frothy-build`, or `.frothy` source
  extensions in this tranche.
- Reopen project-format naming only through a later identity ADR, especially if
  the product/repo identity returns to Froth.

Generated PDF policy:

- Generated PDFs are not active source.
- Do not check generated guide PDFs into the maintained repo surface.
- If a PDF is needed, produce it outside active source as a release artifact or
  external publication artifact.
- Do not reintroduce a checked-in Python PDF renderer as part of the maintained
  proof, build, or docs path.

Dependency policy:

- The maintained core repo budget remains `C`, `Go`, and minimal shell.
- Python is not allowed as checked-in maintained proof, build, release, or docs
  glue. Vendor toolchains such as ESP-IDF may still use their own Python
  internally.
- Node is an explicit exception only for the VS Code extension and release or
  publishability lanes that build/test/package that extension.
- Root edit-loop and extended local gates must not require Node.

## Consequences

- The pre-thesis prune is no longer blocked on project-format naming.
- Existing projects and docs can keep the current `froth*` project-format
  spelling honestly.
- The repo avoids a rename that may be reversed when the broader identity
  returns to Froth.
- Generated PDF policy is explicit: source stays text-first; PDFs are produced
  artifacts, not maintained source.
- Node remains justified, but only because the VS Code extension is a kept
  shipped surface.
- Future work should not treat `froth.toml` or `.froth-build` as accidental
  residue. They are intentionally retained until a later identity ADR says
  otherwise.

## References

- `docs/adr/109-repo-control-surface-and-proof-path.md`
- `docs/adr/111-vscode-extension-owned-control-session.md`
- `docs/adr/113-manifest-owned-project-target-selection.md`
- `docs/adr/120-cli-command-and-home-identity.md`
- `docs/adr/122-public-workshop-release-surface.md`
- `docs/audit/Frothy_Pre_Thesis_Prune_Plan_2026-05.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
