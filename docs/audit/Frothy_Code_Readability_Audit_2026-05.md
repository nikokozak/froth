# Frothy Code Readability Audit 2026-05

Status: active audit ledger
Date: 2026-05-05
Authority:
`docs/spec/Frothy_Language_Spec_v0_1.md`,
Frothy ADRs in `docs/adr/100-*.md`,
the roadmap current-state block,
and `docs/audit/Frothy_Pre_Thesis_Prune_Plan_2026-05.md`

## Purpose

This ledger tracks the post-prune readability audit.

The goal is a tight publishable tree, not a clever rewrite:

- remove or fold redundant files only when ownership is already clear
- make retained legacy substrate intentional at each call site
- keep functions, error paths, reset paths, and allocation stories readable
- replace broad transcript smoke only when focused C or Go coverage proves the
  same behavior honestly
- avoid broad renames, semantic drift, or rearchitecture

## Rubric

Each reviewed file or small file group should answer:

- What is this file's one job?
- Is ownership and lifetime visible?
- Are reset and error paths boring and explicit?
- Does the file hide allocation, device behavior, or persistence effects?
- Is retained `froth_*` substrate intentional rather than accidental?
- Is test or proof coverage direct enough for the behavior?
- Would a serious new reader trust this file without learning old Froth first?

## Audit Log

| Area | Files | Status | Notes | Proof |
|---|---|---|---|---|
| Control proof entrypoint | `tools/frothy/proof.sh`, `tools/cli/cmd/test-runner/suites.go`, `tools/cli/cmd/test-runner/main.go` | reviewed | Shell remains a thin public entrypoint. The Go test runner owns suite composition and injects `FROTHY_TEST_RUNNER_BIN`, so the Go-backed host proofs do not recompile when run through `make test-frothy-proofs`. | `sh tools/frothy/proof.sh control --host-only` with `FROTHY_BINARY=build/test/host-default/Frothy`; `make --no-print-directory test-frothy-proofs` |
| Host REPL proof | `tools/frothy/proof_m8_repl_smoke.sh`, `tools/cli/cmd/test-runner/proofs.go` | tightened | Removed the shell-pipe Ctrl-C multiline case. The Go `proof-ctrlc` proof owns raw-byte multiline interrupt and long-running eval interrupt with stream timing; the REPL smoke keeps the smaller idle Ctrl-C prompt sanity check. | `sh tools/frothy/proof.sh repl` with `FROTHY_BINARY=build/test/host-default/Frothy`; `sh tools/frothy/proof.sh ctrl-c` with `FROTHY_BINARY=build/test/host-default/Frothy` |
| Host inspect proof | `tools/frothy/proof_m8_inspect_smoke.sh`, `tools/frothy/proof_f1_control_smoke.sh`, `tests/frothy_parser_test.c` | tightened | Removed the nested control smoke from the inspect proof. `proof.sh host` already runs `control --host-only` before `inspect`, so `.control` behavior has one owner and `inspect` now checks prompt-level inspection/help behavior. Also removed repeated normalized `show @...` render cases from the transcript; `frothy_parser` already owns detailed surface-render expectations for repeat, logical forms, and local normalization, while `inspect` keeps a representative prompt-level `show @alias` check. | `ctest --test-dir build/test/host-default -R frothy_parser --output-on-failure`; `sh tools/frothy/proof.sh inspect` with `FROTHY_BINARY=build/test/host-default/Frothy`; `make --no-print-directory test-frothy-proofs` |
| Retained type/value boundary | `src/froth_types.h`, `src/frothy_value.[ch]`, `src/frothy_eval.c`, `src/frothy_ffi.c`, `src/frothy_parser.c`, `tests/frothy_parser_test.c`, `tests/frothy_ffi_test.c` | tightened | Removed the unused old Froth 3-bit tagged-cell helper surface from `froth_types.h`. Frothy/32 integer range and two's-complement wrap now have one owner in `frothy_value`; the parser no longer depends on a retained Froth payload macro, protects huge literals before unsigned accumulator wrap, and the uptime helper wraps across the accepted 30-bit immediate range. | `ctest --test-dir build/test/host-default -R 'frothy_(parser\|ffi\|eval)' --output-on-failure`; `make --no-print-directory test-frothy` |
| Build source ownership | `cmake/frothy_runtime_sources.cmake`, `CMakeLists.txt`, `targets/esp-idf/main/CMakeLists.txt` | tightened | Removed the empty snapshot-source return left behind after snapshot ownership moved into `frothy_snapshot.[ch]`. Host and ESP-IDF targets now consume only product and retained-substrate source lists, and the root support-source variable uses the Frothy name consistently. | `make --no-print-directory test-frothy`; `make --no-print-directory test-frothy-slow`; `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with `FROTHY_BINARY=build/test/host-default/Frothy` |
| Record runtime API boundary | `src/frothy_value.[ch]`, `src/frothy_eval.c`, `tests/frothy_eval_test.c` | tightened | Record-definition arity/field lookup no longer forces callers to receive and suppress an unused name. The record runtime entry points now have boring null-output/null-runtime guards, and record payload sizing checks the multiplication before allocation. | `cmake --build build/test/host-default`; `ctest --test-dir build/test/host-default -R 'frothy_(eval\|snapshot)' --output-on-failure`; `make --no-print-directory test-frothy`; `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with `FROTHY_BINARY=build/test/host-default/Frothy` |
| Value object lookup and API guards | `src/frothy_value.c`, `tests/frothy_eval_test.c` | tightened | Centralized live-object bounds checks so classify/render/retain/release and typed getters share the same live-object guard. Added explicit null-runtime/null-output guards for value classify/render/equality/literal conversion and code allocation entry points, plus a refcount overflow guard. | `cmake --build build/test/host-default`; `ctest --test-dir build/test/host-default -R 'frothy_(eval\|snapshot\|ffi)' --output-on-failure`; `make --no-print-directory test-frothy`; `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with `FROTHY_BINARY=build/test/host-default/Frothy` |
| Value payload and object lifecycle | `src/frothy_value.c`, `tests/frothy_eval_test.c` | tightened | Split direct object storage release from child-value release so reset/free paths and refcounted teardown read as separate ownership stories. Object installation now has one helper for reused and newly appended slots, and payload allocation guard coverage includes null entry points. | `cmake --build build/test/host-default`; `ctest --test-dir build/test/host-default -R 'frothy_(eval\|snapshot\|ffi)' --output-on-failure`; `make --no-print-directory test-frothy`; `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with `FROTHY_BINARY=build/test/host-default/Frothy` |
| Eval frame stack and switch error paths | `src/frothy_eval.c`, `tests/frothy_eval_test.c` | tightened | Made the evaluator scratch-slot count explicit, centralized frame-stack used-counter synchronization, and added bounds guards for null/empty eval-program entry points before frame allocation. The large node switch now uses shared helpers for scratch-child phase transitions, completing `out` values, and marking moved scratch slots so repeated ownership/error paths read consistently. | `cmake --build build/test/host-default`; `ctest --test-dir build/test/host-default -R 'frothy_(eval\|snapshot\|ffi)' --output-on-failure`; `make --no-print-directory test-frothy`; `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with `FROTHY_BINARY=build/test/host-default/Frothy` |

## Validation Notes

Current proof-bundle cut:

- `git diff --check`
- `ctest --test-dir build/test/host-default -R frothy_parser --output-on-failure`
- `sh tools/frothy/proof.sh control --host-only` with
  `FROTHY_BINARY=build/test/host-default/Frothy`
- `sh tools/frothy/proof.sh repl` with
  `FROTHY_BINARY=build/test/host-default/Frothy`
- `sh tools/frothy/proof.sh ctrl-c` with
  `FROTHY_BINARY=build/test/host-default/Frothy`
- `sh tools/frothy/proof.sh inspect` with
  `FROTHY_BINARY=build/test/host-default/Frothy`
- `make --no-print-directory test-frothy-proofs`
- `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with
  `FROTHY_BINARY=build/test/host-default/Frothy`

Current retained type/value boundary cut:

- `cmake --build build/test/host-default`
- `ctest --test-dir build/test/host-default -R 'frothy_(parser|ffi|eval)'
  --output-on-failure`
- `make --no-print-directory test-frothy`
- `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with
  `FROTHY_BINARY=build/test/host-default/Frothy`

Current build source ownership cut:

- `make --no-print-directory test-frothy`
- `make --no-print-directory test-frothy-slow`
- `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with
  `FROTHY_BINARY=build/test/host-default/Frothy`

Current record runtime API boundary cut:

- `cmake --build build/test/host-default`
- `ctest --test-dir build/test/host-default -R 'frothy_(eval|snapshot)'
  --output-on-failure`
- `make --no-print-directory test-frothy`
- `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with
  `FROTHY_BINARY=build/test/host-default/Frothy`

Current value object lookup and API guard cut:

- `cmake --build build/test/host-default`
- `ctest --test-dir build/test/host-default -R 'frothy_(eval|snapshot|ffi)'
  --output-on-failure`
- `make --no-print-directory test-frothy`
- `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with
  `FROTHY_BINARY=build/test/host-default/Frothy`

Current value/eval lifecycle and eval-switch readability cut:

- `cmake --build build/test/host-default`
- `ctest --test-dir build/test/host-default -R 'frothy_(eval|snapshot|ffi)'
  --output-on-failure`
- `make --no-print-directory test-frothy`
- `sh tools/frothy/proof.sh m10 /dev/cu.usbserial-0001` with
  `FROTHY_BINARY=build/test/host-default/Frothy`

The direct M10 proof needed an unsandboxed rerun because sandboxed ESP-IDF
failed while `psutil` enumerated host processes. The unsandboxed rerun passed
on `esp32-devkit-v1`.
