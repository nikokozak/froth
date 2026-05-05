# Frothy ADR-127: Test-All CLI Deduplication

**Date**: 2026-05-05
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix B
**Roadmap milestone(s)**: pre-thesis publishability prune
**Inherited Froth references**: Frothy ADR-109, Frothy ADR-126

## Context

After Frothy ADR-126 split the Frothy host lanes, `make test-all` still spent
avoidable time in the CLI portion. The `test-integration` target ran
`go test -tags integration ./cmd`, which includes every ordinary `cmd` package
unit test as well as the tagged integration tests. Those ordinary tests already
run in `test-cli`.

The `test-cli-local` target had the same shape: it ran the `cmd` package again
under a tag that did not add a separate local-runtime test set. Meanwhile, the
actual local-runtime CLI behavior was already represented by two
`TestIntegration...` cases inside the integration-tagged package.

This made `test-all` slower without adding proportional proof value.

## Options Considered

### Option A: Keep The Broad Tagged Package Runs

Leave `test-cli-local` and `test-integration` as full `cmd` package reruns.

Trade-offs:

- Pro: minimal command churn.
- Con: duplicates already-run unit tests.
- Con: hides which integration tests are local-runtime proofs.

### Option B: Remove CLI Local Runtime From `test-all`

Drop `test-cli-local` from `test-all` and rely on the broader integration lane.

Trade-offs:

- Pro: reduces one local command.
- Con: makes the extended gate less explicit about the local-runtime surface.
- Con: leaves local-runtime coverage buried inside a broad integration label.

### Option C: Split CLI Integration By Intent

Keep both public targets, but make each one focused:

- `make test-cli-local` runs the local-runtime CLI integration cases.
- `make test-integration` runs the remaining build/project integration cases.
- `make test-all` continues to run both lanes, but no longer reruns ordinary
  `cmd` unit tests under tagged integration commands.
- the CLI Makefile checks the integration-tagged source list before either
  focused lane runs. Integration tests must live directly in
  `cmd/*integration_test.go`, use `TestIntegration...` names, and be assigned
  to one of the two lane lists.

Trade-offs:

- Pro: preserves extended-gate coverage.
- Pro: removes duplicated unit-test work.
- Pro: names the local-runtime proof surface directly.
- Con: the Makefile carries explicit test-name regexes and a source-list drift
  check that must be updated if new integration cases are added.

## Decision

**Option C.**

The CLI integration surface is split by intent. New CLI integration tests must
land in the target whose name matches the behavior being proved. If a new test
does not fit either lane, update this contract rather than letting
`test-integration` become another broad smoke bucket.

The explicit source-list check is part of the contract: integration-tagged
tests outside `cmd/*integration_test.go` or outside the `TestIntegration...`
naming convention are rejected before the lane runs.

## Consequences

- `make test-all` keeps CLI local-runtime and build/project integration
  coverage while avoiding ordinary `cmd` unit-test duplication.
- `make test-cli-local` is now a real focused lane instead of a tagged rerun of
  ordinary unit tests.
- `make test-integration` measures build/project integration cost more
  honestly.
- The test regexes are explicit maintenance points and fail closed when the
  integration-tagged source list or `TestIntegration...` assignment drifts.

## References

- `Makefile`
- `tools/cli/Makefile`
- `tools/cli/cmd/test-runner/suites.go`
- `.github/workflows/ci.yml`
