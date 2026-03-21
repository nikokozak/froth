#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

. "$SCRIPT_DIR/harness.sh"

build_if_needed

passed=0
failed=0

for test_script in "$SCRIPT_DIR"/test_*.sh; do
  if [ ! -f "$test_script" ]; then
    continue
  fi

  printf '==> %s\n' "$(basename "$test_script")"
  if sh "$test_script"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
done

printf 'Passed: %d\n' "$passed"
printf 'Failed: %d\n' "$failed"

if [ "$failed" -ne 0 ]; then
  exit 1
fi
