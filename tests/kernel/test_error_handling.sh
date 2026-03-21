#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/harness.sh"

FROTH_RUN_DIR=$(new_test_workspace)
run_froth '[ 123 ] catch'
assert_contains '[123 0 -1]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth 'dangerous-reset
[ 1 drop drop ] catch'
assert_contains '[2 0]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth '42 throw'
assert_error 42
assert_contains 'in "throw"'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth '1 drop drop
1 2 +'
assert_error 2
assert_contains '[3]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth '1 drop drop'
assert_contains 'stack underflow in "perm"'
