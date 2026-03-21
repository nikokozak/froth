#!/bin/sh
set -eu

HARNESS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$HARNESS_DIR/../.." && pwd)
DEFAULT_BINARY="$REPO_ROOT/build64/Froth"
HARNESS_TMP_ROOT=${HARNESS_TMP_ROOT:-$(mktemp -d "${TMPDIR:-/tmp}/froth-kernel.XXXXXX")}

LAST_OUTPUT=
LAST_BINARY=
LAST_RUN_DIR=

cleanup_harness() {
  rm -rf "$HARNESS_TMP_ROOT"
}

trap cleanup_harness EXIT HUP INT TERM

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  if [ -n "${LAST_BINARY:-}" ]; then
    printf 'binary: %s\n' "$LAST_BINARY" >&2
  fi
  if [ -n "${LAST_RUN_DIR:-}" ]; then
    printf 'cwd: %s\n' "$LAST_RUN_DIR" >&2
  fi
  if [ -n "${LAST_OUTPUT:-}" ]; then
    printf '%s\n' '--- output ---' >&2
    printf '%s\n' "$LAST_OUTPUT" >&2
    printf '%s\n' '--------------' >&2
  fi
  exit 1
}

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'missing required tool: %s\n' "$1" >&2
    exit 1
  fi
}

build_if_needed() {
  require_tool cmake
  require_tool make

  if [ -x "$DEFAULT_BINARY" ]; then
    return 0
  fi

  mkdir -p "$REPO_ROOT/build64"
  (
    cd "$REPO_ROOT/build64"
    cmake .. -DFROTH_CELL_SIZE_BITS=32
    make
  )
}

new_test_workspace() {
  mktemp -d "$HARNESS_TMP_ROOT/work.XXXXXX"
}

build_posix() {
  require_tool cmake
  require_tool make

  build_dir=$1
  shift

  cmake -S "$REPO_ROOT" -B "$build_dir" -DFROTH_CELL_SIZE_BITS=32 "$@"
  cmake --build "$build_dir"
}

run_froth() {
  input=${1-}

  build_if_needed
  require_tool timeout

  LAST_BINARY=${FROTH_BINARY:-$DEFAULT_BINARY}
  LAST_RUN_DIR=${FROTH_RUN_DIR:-}
  if [ -z "$LAST_RUN_DIR" ]; then
    LAST_RUN_DIR=$(new_test_workspace)
  fi

  if [ ! -x "$LAST_BINARY" ]; then
    fail "binary not found: $LAST_BINARY"
  fi

  set +e
  output=$(
    cd "$LAST_RUN_DIR" &&
    {
      sleep "${FROTH_BOOT_DELAY:-2}"
      if [ -n "$input" ]; then
        printf '%s\n' "$input"
      fi
      printf '\004'
    } | timeout "${FROTH_TIMEOUT:-8}" "$LAST_BINARY" 2>&1
  )
  status=$?
  set -e

  LAST_OUTPUT=$output

  if [ "$status" -ne 0 ]; then
    fail "Froth exited with status $status"
  fi
}

assert_output() {
  expected=$1
  if [ "$LAST_OUTPUT" != "$expected" ]; then
    fail "output mismatch"
  fi
}

assert_contains() {
  substring=$1
  if ! printf '%s' "$LAST_OUTPUT" | grep -F -- "$substring" >/dev/null 2>&1; then
    fail "expected output to contain: $substring"
  fi
}

assert_not_contains() {
  substring=$1
  if printf '%s' "$LAST_OUTPUT" | grep -F -- "$substring" >/dev/null 2>&1; then
    fail "expected output to not contain: $substring"
  fi
}

assert_error() {
  assert_contains "error($1)"
}
