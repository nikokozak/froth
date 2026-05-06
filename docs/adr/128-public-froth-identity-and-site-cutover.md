# Frothy ADR-128: Public Froth Identity And Site Cutover

**Date**: 2026-05-06
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, section 1 and Appendix B
**Roadmap milestone(s)**: public Froth identity and site parity cutover
**Related ADRs**: Frothy ADR-100, Frothy ADR-120, Frothy ADR-122, Frothy ADR-124
**Inherited Froth references**: original Froth repo and `FrothDocs`, to be archived as OldFroth

## Context

The repo previously separated the new language from inherited Froth by adopting
the public `Frothy` product name. Frothy ADR-100 and Frothy ADR-120 moved the
release, CLI, install, Homebrew, editor, and docs surface toward `Frothy` /
`frothy`, while intentionally leaving internal `froth_*` substrate and project
format names alone.

That split solved the immediate fork-boundary problem, but it is no longer the
desired release identity. The public product should be called `Froth`, and the
original Froth should become `OldFroth`.

This creates three risks:

- a repo-wide rename would consume release time and destabilize working code
- keeping `Frothy` in public surfaces would ship the wrong product identity
- moving the new site to `frothlang.org` before it reaches old-site content
  parity would make the launch feel thinner than the old Froth site

The current source and website state also matters:

- the maintained runtime and toolchain still use many `frothy_*` internal names
- the project format already uses `froth.toml`, `.froth-build`, and `.froth`
  names under Frothy ADR-124
- the current new public docs source lives in the sibling `FrothyDocs` repo and
  publishes at `frothy.frothlang.org`
- the current old Froth site source lives in the sibling `FrothDocs` repo and
  publishes at `frothlang.org`

## Options Considered

### Option A: Keep Frothy Public And Explain The Split

Keep publishing the new language as `Frothy`, leave the original as `Froth`,
and use docs to explain the difference.

Trade-offs:

- Pro: avoids churn.
- Pro: preserves the identity choices already recorded in Frothy ADR-100 and
  Frothy ADR-120.
- Con: ships the wrong name.
- Con: leaves the strongest domain and brand attached to the older language.
- Con: forces every announcement to teach a naming compromise before teaching
  the language.

### Option B: Rename Everything Before Announcement

Rename repo paths, C symbols, Go modules, CMake targets, test names, scripts,
docs, website repos, package names, extension internals, and release artifacts
from `Frothy` to `Froth` in one broad sweep.

Trade-offs:

- Pro: makes the tree visually uniform.
- Pro: eliminates most future identity cleanup.
- Con: creates very high mechanical churn before release.
- Con: risks breaking the maintained hardware proof path for cosmetic reasons.
- Con: spends release time on internal names users do not see.
- Con: conflicts with the roadmap cut rule that explicitly avoided a broad
  internal `froth_*` / `frothy_*` symbol rename during the publishability pass.

### Option C: Public Identity Cutover With Internal Names Allowed

Move all user-facing surfaces to `Froth`, archive the original product as
`OldFroth`, and keep `Frothy` where it is internal implementation history or
private repo-control terminology. Block the `frothlang.org` cutover on a
content-parity ledger against the old site.

Trade-offs:

- Pro: ships the desired public name.
- Pro: keeps the release cut focused on surfaces users actually see.
- Pro: makes `froth.toml`, `.froth-build`, and `.froth` intentional public
  names rather than deferred residue.
- Pro: lets the new site inherit the old site's shape without preserving old
  stack-language semantics.
- Con: leaves internal and historical docs with `Frothy` names for now.
- Con: requires careful grep gates so the public surface does not leak the old
  transitional name.
- Con: requires an OldFroth archive story before the domain and repo rename can
  be clean.

## Decision

**Option C.**

The public product name is `Froth`.

The original Froth product, repository, documentation, package lineage, and
site become `OldFroth` wherever they must remain visible for history,
compatibility, or reference.

The release cut must separate public identity from internal implementation
identity:

- public website, README, install docs, guides, tutorials, reference pages,
  release assets, package metadata, Homebrew formula text, VS Code display
  text, CLI help, CLI version text, and public workshop materials use `Froth`
- old-language archive pages use `OldFroth`
- public CLI state uses `FROTH_HOME` and `~/.froth`; the old transitional home
  variable may remain as an undocumented compatibility fallback
- public derived build outputs use `froth` names, including `build/froth`,
  `.froth-build/firmware/froth`, and `.froth-build/runtime.froth`
- historical comparisons may say that Froth keeps or changes behavior from
  OldFroth, but must not present OldFroth as the current language
- public C extension examples and project FFI templates use `froth_*` /
  `FROTH_*` aliases, even while the maintained implementation continues to
  back those aliases with internal `frothy_*` code
- `Frothy` may remain in internal C symbols, Go package paths, CMake function
  names, test labels, script paths, historical ADR filenames, and repo-control
  records while this keeps the cut focused and low-risk
- any `frothy` executable, package, domain, extension ID, or setting that is
  visible to a new user is transitional debt and must either be renamed,
  hidden, or explicitly scoped as a temporary compatibility fallback before the
  public announcement

The site cutover is gated by
`docs/roadmap/Froth_Public_Site_Cutover_And_Content_Parity.md`:

- `FrothyDocs` is the basis for the new main `frothlang.org` site
- `FrothDocs` becomes the OldFroth archive source, or its content is moved
  into an explicit OldFroth archive location
- the old site's top-level information architecture remains the parity model:
  home, install, guide, tutorials, reference, and how-it-is-different
- the new site may add `machine` and `workshop`, but those sections do not
  substitute for guide, tutorial, or reference parity
- DNS or Pages configuration must not move the new site to `frothlang.org`
  until the parity ledger is green or every remaining gap has an explicit
  cutover exception

This ADR supersedes the public identity portions of Frothy ADR-100 and Frothy
ADR-120. It does not rewrite the accepted language semantics, runtime value
model, persistence contract, FFI boundary, or project-format decision.

## Consequences

- The announcement can use one clean name: Froth.
- The old product has a clear non-current name: OldFroth.
- `froth.toml`, `.froth-build`, and `.froth` are now aligned with the future
  public name rather than waiting for a `frothy*` rename.
- The public CLI command should return to `froth`; any `frothy` command should
  be undocumented compatibility only until it is removed or hidden.
- The CMake host target may remain internally named `Frothy`, but the produced
  binary visible to users is `froth`.
- The public editor package should publish as `NikolaiKozak.froth`, expose
  `Froth:` command titles, and use `froth.cliPath` / `froth.port` settings.
- The GitHub repo/domain ownership sequence must be ordered carefully:
  OldFroth must move out of the way before the new project takes over the
  primary `froth` repo and `frothlang.org` site surfaces.
- Internal source names can remain stable until a later low-risk cleanup,
  keeping the immediate release work out of a broad rename trap.
- Public-identity proof scripts need explicit allowlists for historical and
  internal-control mentions of `Frothy`.

## References

- `docs/adr/100-repo-and-release-identity.md`
- `docs/adr/120-cli-command-and-home-identity.md`
- `docs/adr/122-public-workshop-release-surface.md`
- `docs/adr/124-pre-thesis-project-format-and-dependency-policy.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
- `docs/roadmap/Froth_Public_Site_Cutover_And_Content_Parity.md`
- `/Users/niko/Developer/FrothyDocs`
- `/Users/niko/Developer/FrothDocs`
