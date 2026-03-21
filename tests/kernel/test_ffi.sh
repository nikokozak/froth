#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/harness.sh"

FROTH_RUN_DIR=$(new_test_workspace)
run_froth 'dangerous-reset
"emit-me" s.emit cr
"abc" s.len
"abc" 1 s@
"a" "a" s.=
"a" "b" s.='
assert_contains 'emit-me'
assert_contains '[3]'
assert_contains '[3 98]'
assert_contains '[3 98 -1]'
assert_contains '[3 98 -1 0]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth 'info'
assert_contains 'Froth v0.1.0'
assert_contains '32-bit cells'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth ': alpha 1 ; : beta 2 ; words'
assert_contains 'alpha |'
assert_contains 'beta |'
