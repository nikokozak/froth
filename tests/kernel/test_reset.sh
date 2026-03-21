#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/harness.sh"

FROTH_RUN_DIR=$(new_test_workspace)
run_froth '111 222
.s
dangerous-reset
.s'
assert_contains '[111 222]'
assert_contains 'dangerous-reset
reset'
assert_contains '.s
[]
[]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth ': temp 42 ;
dangerous-reset
temp'
assert_error 4

FROTH_RUN_DIR=$(new_test_workspace)
run_froth 'dangerous-reset
: newer 7 ;
newer'
assert_contains '[7]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth 'dangerous-reset
"after-reset" s.emit'
assert_contains 'after-reset[]'
