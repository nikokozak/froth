# Frothy ADR-126: Frothy Test-Speed Split

**Date**: 2026-05-05
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 8 and Appendix B
**Roadmap milestone(s)**: pre-thesis publishability prune
**Inherited Froth references**: Frothy ADR-109

## Context

Frothy ADR-109 made the repo root Frothy-first and introduced a predictable
test surface. The first publishability-prune pass also moved the broad host
proof rehearsal out of the default `make test` edit loop.

One heavy edge remained: `make test-frothy` still looked like a focused
Frothy-only target but actually ran fast host C tests, slow CMake/config smoke
tests, and every host proof script. On this checkout the fast Frothy CTest lane
is well under a second after the profile build is current, while the slow CTest
lane is several seconds and the host proof bundle is roughly 45 seconds.

That shape makes ordinary runtime edits feel slower than they are and
encourages proof-script growth where focused C or Go tests would be clearer.

## Options Considered

### Option A: Keep `test-frothy` As The Full Host Bundle

Leave the command unchanged and rely on contributors to remember the lower
level test-runner commands.

Trade-offs:

- Pro: no command contract changes.
- Con: the target name hides the cost center.
- Con: routine Frothy runtime work keeps paying for proof scripts by default.

### Option B: Remove Host Proofs From Local Gates

Make all proof scripts manual-only and keep local gates to unit-style tests.

Trade-offs:

- Pro: fastest local gates.
- Con: loses the accepted ADR-109 host proof path for extended local checks.
- Con: makes integration regressions easier to miss before real-device proof.

### Option C: Split Fast, Slow, Proof, And Full Frothy Lanes

Keep the same proof surface, but expose its cost centers directly:

- `make test-frothy`: fast Frothy host CTests only
- `make test-frothy-slow`: slower CMake/config smoke CTests
- `make test-frothy-proofs`: `tools/frothy/proof.sh host`
- `make test-frothy-full`: all Frothy host CTests plus host proofs
- `make test-all`: unchanged extended local gate, still including slow Frothy
  CTests and host proofs

Trade-offs:

- Pro: routine Frothy edits get a fast, honest target.
- Pro: proof scripts remain available and named instead of being hidden.
- Pro: CI and local callers can parallelize or select lanes by risk.
- Con: callers that assumed `make test-frothy` meant "everything Frothy" must
  move to `make test-frothy-full`.

## Decision

**Option C.**

Frothy keeps host proofs as part of the extended local gate, but makes the
Frothy-specific lanes proportional and explicit. New smoke proofs should not be
added to the fast Frothy lane unless focused C or Go coverage cannot exercise
the behavior honestly.

This amends Frothy ADR-109's `make test-frothy` wording. ADR-109 remains the
repo-control authority for the overall proof surface; this ADR defines the
post-prune Frothy target split.

## Consequences

- `make test` remains the fast default local gate.
- `make test-frothy` becomes useful for tight runtime/language edit loops.
- `make test-frothy-full` preserves the previous `test-frothy` breadth.
- `make test-all` remains the extended `C` + `Go` + shell local gate and still
  avoids Node.
- CI covers the slower Frothy lanes as separate jobs instead of leaving them
  as local-only proof coverage.
- Real-device proofs remain explicit and are still required for sign-off.
- Slow proof growth is easier to notice because it lands in a named proof lane.

## References

- Frothy ADR-109
- `Makefile`
- `.github/workflows/ci.yml`
- `tools/cli/cmd/test-runner/suites.go`
- `tools/frothy/proof.sh`
