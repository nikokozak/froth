#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/harness.sh"

FROTH_RUN_DIR=$(new_test_workspace)
run_froth '"hello" s.emit'
assert_contains 'hello[]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth ': greet "hello" s.emit ; greet'
assert_contains 'hello[]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth "'msg \"kept\" def msg s.emit"
assert_contains 'kept[]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth "\"hold\" s.keep 'kept swap def kept s.emit"
assert_contains 'hold[]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth "\"hi\" dup 'x swap def s.emit x s.emit"
assert_contains 'hihi[]'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth "'msg \"saved\" def save msg s.emit"
assert_contains 'saved[]'
assert_not_contains 'cannot save transient string'

source=
i=1
while [ "$i" -le 25 ]; do
  source="${source}\"s${i}\" s.emit cr
"
  i=$((i + 1))
done

FROTH_RUN_DIR=$(new_test_workspace)
run_froth "$source"
assert_contains 's1'
assert_contains 's25'
