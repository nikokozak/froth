# Frothy

Frothy is a small live lexical language for programmable devices.

Frothy `v0.1` is functionally closed.

The live roadmap milestone is `pre-thesis publishability prune`. The
non-workshop work now leads: keep the public thesis-facing codebase light,
readable, and dependency-bounded; keep generated PDFs out of active source;
keep Python out of maintained proof, build, release, and docs glue; and run the
shipped-surface local gate from this checkout.

Workshop material remains in-tree as retained operational reference, but it is
not the active queue for this pass.

The forward queue after the thesis prune is explicit:

- FFI boundary quality and porting discipline
- small useful core library growth
- robust string support
- measured performance tightening
- direct-control tooling improvements
- later workspace/image-flow growth only after it is intentionally reprioritized

See `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` for the
kept-vs-deferred stack, including the deferred workshop-operational queue.

The repo reuses inherited Froth substrate where that is the simplest working
path, but Froth's old roadmap, stack-centric user model, and language
priorities are not active policy here.

## Start Here

Use the smallest maintained doc set for the thesis/public repo path:

- `docs/spec/Frothy_Language_Spec_v0_1.md`: accepted current language contract
- `docs/guide/Frothy_From_The_Ground_Up.md`: narrative guide to the current
  system
- `docs/adr/README.md`: Frothy `100`-series authority index
- `docs/adr/124-pre-thesis-project-format-and-dependency-policy.md`: current
  project-format, generated-PDF, Python, and Node policy
- `docs/audit/Frothy_Pre_Thesis_Prune_Plan_2026-05.md`: active prune plan
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`: live current-state block
- `PROGRESS.md` and `TIMELINE.md`: short operational note and movable queue

## Deferred Workshop Materials

The workshop path is retained but deferred for the current thesis-prune pass.
These assets and listings remain the recorded workshop promise when that queue
is reprioritized:

| Surface | Release surface | Workshop promise |
| --- | --- | --- |
| CLI release | `frothy-v<version>-darwin-arm64.tar.gz`, `frothy-v<version>-darwin-amd64.tar.gz`, `frothy-v<version>-linux-amd64.tar.gz` | macOS via Homebrew is the preferred attendee path; Linux x86_64 can use the release tarball directly |
| VS Code | Marketplace listing `NikolaiKozak.frothy`, with matching `frothy-vscode-v<extension-version>.vsix` fallback | supported on the same machines that can already run the installed CLI |
| Firmware / recovery | workshop-board recovery for `esp32-devkit-v4-game-board` is maintainer-only from the repo checkout and [boards/esp32-devkit-v4-game-board/WORKSHOP.md](/Users/niko/Developer/Frothy/boards/esp32-devkit-v4-game-board/WORKSHOP.md) | attendees do not flash; maintainers carry preflashed `esp32-devkit-v4-game-board` boards |
| Source build | checkout build via `make build` | maintainer path, not required before the workshop |
| Workshop repo | [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop) containing `README.md` and `starter.frothy` | attendees open and edit `starter.frothy` against the preflashed demo board |

Windows, extra boards, and custom toolchain setups are not part of the
maintained attendee promise for this tranche.

Board targets such as `esp32-devkit-v1` and `esp32-devkit-v4-game-board`
refer to specific hardware revisions. They are not Frothy protocol or repo
generation markers. The workshop promise is narrower and currently centers on
the mounted preflashed `esp32-devkit-v4-game-board`, but `esp32-devkit-v1`
remains an accepted board model in the repo.

## Naming Matrix

The published naming split is explicit for now:

| Thing | Name today |
| --- | --- |
| Product, repo, docs, release assets, Homebrew formula, and editor surface | `Frothy` / `frothy` |
| Installed CLI command from released assets | `frothy` |
| Repo-local checkout CLI build | `tools/cli/frothy-cli` |
| Host runtime built from source | `build/Frothy` |

The CLI rename tranche is now landed: Frothy owns the public product and
release identity, and the installed/repo-local CLI surface is `frothy`.
VS Code still keeps legacy `froth` discovery as a temporary compatibility path
during the transition.

## Workshop Install (Deferred)

The attendee quickstart lives in
`docs/guide/Frothy_Workshop_Install_Quickstart.md`.
The in-room prompt and recovery cheat sheet lives in
`docs/guide/Frothy_Workshop_Quick_Reference.md`.

Use that guide for the exact Homebrew, release-tarball, and VSIX install
commands when the workshop queue resumes.

The retained workshop assumptions are:

- attendees use the installed CLI command `frothy`
- attendees do not need a repo checkout, `esp-idf`, or source builds before
  they arrive
- the current workshop run uses a preflashed `esp32-devkit-v4-game-board`
  proto board
- the public workshop repo is [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop)
- if VS Code cannot find `frothy` on `PATH`, set `frothy.cliPath` to the
  absolute path of the installed binary; legacy `froth` fallback remains
  available during the transition

The retained editor path stays on the accepted direct-control surface:

- VS Code owns one helper child per window
- the helper owns one direct control session at a time
- there is no daemon, shared port owner, or local editor runtime in the
  maintained workshop path

`Send Selection / Form` is intentional additive eval.
`Send File` is whole-file `reset + eval`; if the connected firmware is too old
for control `reset`, the extension warns before any explicitly unsafe additive
fallback and otherwise asks you to upgrade or reflash.

## Build And Test

From a checkout:

```sh
make build
make run
make test
make test-all
make test-publishability
```

Optional deferred or extension lanes:

```sh
make test-vscode
make test-vscode-board PORT=/dev/...
make test-workshop
```

The host build produces:

- `build/Frothy`: primary Frothy host runtime

The maintained test contract is:

- `make test`: fast self-contained local gate (`C`, `Go`, `Shell`)
- `make test-all`: exhaustive local gate (`C`, `Go`, `Shell`)
- `make test-publishability`: full shipped-surface local gate (`make test-all` plus `make test-vscode`)
- `make test-vscode`: explicit extension-local `Node` lane
- `make test-vscode-board PORT=/dev/...`: explicit real-device extension lane
- `make test-workshop`: deferred workshop-only local checks
- `make test-list`: list maintained suites and profiles
- `sh tools/frothy/proof.sh m10 <PORT>`: generic `esp32-devkit-v1`
  real-device proof for blink, boot persistence, cells/ADC, and board surface
- `sh tools/frothy/proof.sh workshop-v4 <PORT>`: focused non-interactive
  real-device `esp32-devkit-v4-game-board` workshop proof
- `sh tools/frothy/proof.sh workshop-v4 --live-controls <PORT>`: optional
  manual joystick/button extension to the same board proof

Run the currently shipped CLI as `frothy`:

```sh
frothy --version
frothy doctor
frothy build
frothy flash
frothy connect
frothy send src/main.froth
```

## Active Docs

- `docs/spec/Frothy_Language_Spec_v0_1.md`: normative Frothy language and
  interactive-profile spec
- `docs/spec/Frothy_Language_Spec_vNext.md` and
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`: draft next-stage
  language direction without widening current behavior
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`: live control surface and
  accepted milestone roadmap
- `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`: post-`v0.1`
  queue note and deferred workshop gate
- `docs/adr/README.md`: ADR authority split and Frothy `100`-series index
- `PROGRESS.md`: thin operational note
- `TIMELINE.md`: movable checkbox ledger

## Reference Material

The original Froth repo at `/Users/niko/Developer/Froth` is reference material
only. Use it for substrate reuse, boot/persistence/transport background, and
implementation salvage where explicitly adopted. Do not treat its roadmap,
language semantics, repo-local startup guidance, or implementation priorities
as active Frothy policy.

See `docs/reference/Froth_Substrate_References.md` and
`docs/reference/Frothy_Retained_Substrate_Manifest.md` for the curated
reference set and the current retained-substrate boundary.

Historical Froth-era design notes now live under `docs/archive/`.

## Repo Shape

- The repo root is the source of truth for kernel, platform, board, and target
  sources.
- `make sdk-payload` generates the CLI's embedded SDK archive from that source
  tree; the repo does not track a maintained mirror under
  `tools/cli/internal/sdk`.
- Stale bootstrap drafts, legacy runtime code, and archived Froth design notes
  are not part of the live control surface.
