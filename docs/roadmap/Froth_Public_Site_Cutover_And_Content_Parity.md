# Froth Public Site Cutover And Content Parity

Status: active release-control ledger
Date: 2026-05-06
Authority:
Frothy ADR-128,
`docs/spec/Frothy_Language_Spec_v0_1.md`,
Frothy ADR-100,
Frothy ADR-120,
Frothy ADR-122,
Frothy ADR-124,
and the roadmap current-state block

## Purpose

This ledger keeps the public rename and website cutover out of a quagmire.

The goal is not a repo-wide internal rename. The goal is to launch the new
language as **Froth**, rename the original product to **OldFroth**, and move the
new site to `frothlang.org` only after it matches the old site in content
coverage, navigational shape, and presentation quality.

## Source Repos

- New public site basis: `/Users/niko/Developer/FrothyDocs`
- Old site and parity reference: `/Users/niko/Developer/FrothDocs`
- Current runtime/control source: `/Users/niko/Developer/Frothy`
- Original product source to archive as OldFroth: `/Users/niko/Developer/Froth`

## Cutover Rules

- Do not move DNS or GitHub Pages custom-domain ownership until the parity
  ledger is green or explicitly waived.
- Do not keep two living public sites for the same current product.
- Preserve old-site shape first: home, install, guide, tutorials, reference,
  and how-it-is-different.
- Add `machine` and `workshop` as new Froth strengths, but do not count them as
  substitutes for missing tutorials or reference pages.
- Keep `Frothy` out of public user-facing surfaces unless the page is historical
  or explicitly describing internal implementation history.
- Use `OldFroth` for the original language and old site after the rename.
- Keep internal `frothy_*` source names, CMake labels, script paths, and
  historical ADR filenames out of scope unless they leak into user-facing docs,
  CLI output, package metadata, or release assets.

## Definition Of Done

- [x] New identity ADR accepted and referenced from the live queue.
- [~] New site title, nav, footer, and metadata say `Froth`; CNAME and the
  final `frothlang.org` base URL stay held until deployment cutover.
- [x] New site source has a content-parity page or ledger checked in beside the
  site source.
- [x] Every old public route maps to a new Froth route, an OldFroth archive
  route, or an explicit cutover exception.
- [x] New site has at least the old site's top-level shape:
  `/`, `/install/`, `/guide/`, `/tutorials/`, `/reference/`,
  `/what-makes-froth-different/`.
- [x] New site includes the new additive sections:
  `/machine/` and `/workshop/`.
- [x] Tutorials exist as first-class pages, not only workshop material.
- [x] Reference depth covers CLI, editor, project/build, interactive profile,
  image/persistence, FFI, words, and hardware.
- [~] Public examples use current Froth syntax and maintained `froth`
  commands on the implemented tutorial/reference/install/workshop paths; the
  remaining old-shape guide gaps still need review.
- [~] Public install content is honest about availability; the complete
  install path still waits on release artifacts.
- [x] Site search indexes the guide, tutorials, machine, workshop, and reference
  surfaces.
- [x] Smoke checks fail on missing routes, broken internal links, placeholder
  pages, stale public `Frothy` branding, and stale current-product `frothy`
  commands.
- [~] Old Froth site and repo are archived under `OldFroth`; the new site now
  has an `/oldfroth/` marker route and the old-site source has an archive
  banner plus `oldfroth.frothlang.org` base URL, but DNS/Pages and original
  repo moves are still held.
- [ ] Final cutover is validated locally with the site smoke script and by
  fetching the published Pages URL after deployment.

## Proposed Public Shape

```text
/
/install/
/guide/
  /guide/00-installation/
  /guide/01-what-is-froth/
  /guide/02-getting-started/
  /guide/03-values-and-names/
  /guide/04-definitions/
  /guide/05-code-locals-and-blocks/
  /guide/06-control-flow/
  /guide/07-errors-and-recovery/
  /guide/08-text-and-io/
  /guide/09-hardware/
  /guide/10-persistence/
  /guide/11-where-to-go-next/
/tutorials/
  /tutorials/blink-an-led/
  /tutorials/interactive-workflow/
  /tutorials/read-inputs/
  /tutorials/build-a-small-game/
/machine/
/workshop/
/reference/
  /reference/words/
  /reference/cli/
  /reference/editor/
  /reference/project-and-build/
  /reference/interactive-profile/
  /reference/image-and-persistence/
  /reference/ffi/
  /reference/hardware/
/what-makes-froth-different/
/oldfroth/ or https://oldfroth.frothlang.org/
```

## Parity Ledger

Status keys:

- `[x]` covered enough for cutover
- `[~]` exists, but needs rename, depth, route, or polish before cutover
- `[ ]` missing or too thin
- `[-]` should move to OldFroth archive or be replaced by current Froth concept

| Old Froth route | New Froth route | Status | Required action |
| --- | --- | --- | --- |
| `/` | `/` | `[x]` | Homepage identity, first-viewport signal, visual asset, and first code example are current enough for cutover review. |
| `/install/` | `/install/` | `[~]` | Launch-path commands, Homebrew formula, direct archive names, VSIX name, and `froth doctor` are staged; live availability still waits on release artifacts. |
| `/guide/` | `/guide/` | `[~]` | Preserve chaptered guide shape while keeping current Froth semantics. |
| `/guide/00-installation/` | `/guide/00-installation/` or `/install/` | `[ ]` | Decide whether install is duplicated as chapter 00 or linked from guide; old shape needs an obvious guide entry. |
| `/guide/01-what-is-froth/` | `/guide/01-what-is-froth/` | `[~]` | Rename route/title and keep the current live-image/lexical framing. |
| `/guide/02-getting-started/` | `/guide/02-getting-started/` | `[ ]` | Add a hardware-first first-session page distinct from values/reference chapters. |
| `/guide/03-the-stack/` | OldFroth archive plus current guide note | `[-]` | Do not preserve stack-centric teaching as current Froth. Add a short "what changed from OldFroth" bridge. |
| `/guide/04-words-and-definitions/` | `/guide/03-values-and-names/`, `/guide/04-definitions/` | `[~]` | Split current slots/rebinding from function definition flow; make the old words page map explicit. |
| `/guide/05-perm-and-named/` | OldFroth archive plus `/guide/04-definitions/` | `[-]` | Archive `perm` as OldFroth-specific; do not teach it as current Froth. |
| `/guide/06-quotations-and-control/` | `/guide/05-code-locals-and-blocks/`, `/guide/06-control-flow/` | `[~]` | Current code/block chapters exist but need route/name alignment and fuller guide pacing. |
| `/guide/07-error-handling/` | `/guide/07-errors-and-recovery/` | `[ ]` | Add current runtime error, interrupt, safe boot, restore, and recovery guidance. |
| `/guide/08-strings-and-io/` | `/guide/08-text-and-io/` | `[ ]` | Add or explicitly defer current text/I/O coverage. |
| `/guide/09-talking-to-hardware/` | `/guide/09-hardware/`, `/machine/` | `[~]` | Current hardware intro exists but needs old-shape route and clearer split from Machine. |
| `/guide/10-snapshots-and-persistence/` | `/guide/10-persistence/`, `/reference/image-and-persistence/` | `[~]` | Current persistence reference exists; guide page needs more narrative depth. |
| `/guide/11-where-to-go-next/` | `/guide/11-where-to-go-next/` | `[~]` | Rename and route-align. |
| `/guide/12-ffi-and-c/` | `/guide/12-ffi-and-c/`, `/reference/ffi/` | `[~]` | Current FFI guide/reference exists, but old site has deeper nested FFI material. |
| `/tutorials/` | `/tutorials/` | `[x]` | Top-level tutorials section exists in the new site nav and smoke checks. |
| `/tutorials/blink-an-led/` | `/tutorials/blink-an-led/` | `[x]` | Rewritten around current `froth` CLI, `to ... with ...`, `LED_BUILTIN`, save, and wipe. |
| `/tutorials/interactive-workflow/` | `/tutorials/interactive-workflow/` | `[x]` | Rewritten around current CLI/editor control session, live image inspection, interrupt, and save workflow. |
| `/tutorials/read-a-sensor/` | `/tutorials/read-inputs/` | `[x]` | Rewritten for joystick and knob helpers on the Froth Machine. |
| `/tutorials/read-a-button/` | `/tutorials/read-inputs/` | `[x]` | Folded into the current input tutorial and Machine controls path. |
| `/tutorials/fade-an-led/` | `/tutorials/fade-an-led/` or archive | `[ ]` | Keep only if maintained PWM/LED fading surface exists; otherwise mark as OldFroth archive. |
| `/tutorials/drive-a-servo/` | `/tutorials/drive-a-servo/` or archive | `[ ]` | Keep only if current hardware support exists. |
| `/tutorials/build-a-calculator/` | `/tutorials/build-a-small-tool/` or archive | `[ ]` | Replace stack-language calculator with current lexical example or archive as OldFroth. |
| `/tutorials/advent-of-code-grid-scan/` | `/tutorials/grid-scan/` or `/machine/patterns/` | `[~]` | Machine patterns partially cover this; decide whether it graduates to tutorial. |
| `/tutorials/advent-of-code-safe-dial/` | `/tutorials/safe-dial/` or `/machine/patterns/` | `[~]` | Machine patterns partially cover this; decide whether it graduates to tutorial. |
| `/reference/` | `/reference/` | `[~]` | New reference index exists, but needs old-shape route coverage. |
| `/reference/words/` | `/reference/words/` | `[~]` | Current words reference exists; verify complete against maintained base image. |
| `/reference/cli/` | `/reference/cli/` | `[x]` | Public command name is `froth` and release/install/project distinctions are covered enough for cutover review. |
| `/reference/vscode/` | `/reference/editor/` | `[x]` | Public editor reference now covers visible commands, settings, and recovery flow. |
| `/reference/build-options/` | `/reference/project-and-build/` | `[x]` | Target/board/project build reference now exists. |
| `/reference/profiles/` | `/reference/interactive-profile/` | `[~]` | Current profile page exists; rename away from Frothy and verify control-session details. |
| `/reference/snapshot-format/` | `/reference/image-and-persistence/` | `[~]` | Current page exists; decide how much binary-format detail belongs publicly. |
| `/reference/ffi/` | `/reference/ffi/` | `[x]` | Structured FFI section now exists and uses public `froth_*` / `FROTH_*` C aliases. |
| `/reference/ffi/project-ffi/` | `/reference/ffi/project-ffi/` | `[x]` | Project FFI route now documents manifest, validation, build path, and ownership rules. |
| `/reference/ffi/project-ffi-example/` | `/reference/ffi/project-ffi-example/` | `[x]` | Current example now uses `froth_ffi.h`, `froth_project_bindings`, and current Froth call syntax. |
| `/reference/ffi/board-ffi-example/` | `/reference/ffi/board-ffi-example/` | `[x]` | Current maintained-board example route now exists. |
| `/reference/ffi/how-it-works/` | `/reference/ffi/how-it-works/` | `[x]` | Runtime model and public C surface route now exists. |
| `/reference/hardware/` | `/reference/hardware/` | `[~]` | New hardware reference exists and is Machine-oriented; add generic maintained-board map. |
| `/reference/hardware/gpio/` | `/reference/hardware/gpio/` or `/reference/hardware/base-image/` | `[~]` | Current base image has GPIO coverage; route decision needed. |
| `/reference/hardware/timing/` | `/reference/hardware/timing/` or `/reference/hardware/utilities/` | `[~]` | Current utilities page exists but is thin. |
| `/reference/hardware/i2c/` | OldFroth archive or current hardware route | `[-]` | Keep only if current release exposes I2C. |
| `/reference/hardware/uart/` | OldFroth archive or current hardware route | `[-]` | Keep only if current release exposes UART. |
| `/what-makes-froth-different/` | `/what-makes-froth-different/` | `[x]` | Renamed from Frothy and framed against OldFroth. |
| Search index | Search index | `[x]` | Smoke checks now cover tutorials, core terms, and stale generated public identity. |
| Visual assets | Visual assets | `[x]` | Homepage now has a checked-in `home-hero.png` asset. |
| Old product archive | `/oldfroth/` or `oldfroth.frothlang.org` | `[~]` | New site has `/oldfroth/`; old-site source has `OldFroth` title, archive banner, and `oldfroth.frothlang.org` base URL; DNS/Pages, redirect policy, and original repo move remain gated. |

## Immediate Work Order

1. Land Frothy ADR-128 and this ledger in the runtime/control repo.
2. In `FrothyDocs`, add a matching `_internal/content-parity.md` copied from
   this ledger or generated from it.
3. Rename visible site identity from Frothy to Froth while leaving internal
   file names alone.
4. Restore old-site top-level shape by adding `/tutorials/` and missing
   reference/editor/project pages.
5. Add visual assets and verify the site no longer looks thinner than the old
   Froth site.
6. Build public identity smoke checks:
   route existence, internal link check, search-index terms, placeholder ban,
   and stale public `Frothy`/`frothy` command scan.
7. Close public CLI/runtime/editor/release/workshop identity leaks in the
   runtime repo and stage the old-site source as `OldFroth`.
8. Only after those checks pass, move Pages/DNS and archive OldFroth.

## Focused Proofs

For this control-ledger step:

```sh
rg -n "Frothy ADR-128|Froth_Public_Site_Cutover" docs/adr docs/roadmap README.md PROGRESS.md TIMELINE.md
sh tools/frothy/proof_control_surface_docs.sh
```

For the later website step in `/Users/niko/Developer/FrothyDocs`:

```sh
./tools/smoke.sh
rg -n "\bFrothy\b|\bfrothy\b" content layouts static hugo.toml README.md
```

The second grep will need an allowlist for internal script filenames and
historical archive pages. It should fail closed for public current-product
copy.
