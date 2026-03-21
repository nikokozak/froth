#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/harness.sh"

FROTH_RUN_DIR=$(new_test_workspace)
run_froth "'saved-word [ 99 ] def save"
assert_contains '[]'

run_froth 'saved-word'
assert_contains '[99]'

run_froth 'wipe'
assert_contains 'reset'

run_froth 'saved-word'
assert_error 4

USER_PROGRAM_DIR=$(new_test_workspace)
USER_PROGRAM_PATH="$USER_PROGRAM_DIR/user_program.froth"
cat >"$USER_PROGRAM_PATH" <<'EOF'
: autorun "USER-PROGRAM" s.emit cr ;
EOF

USER_BUILD_DIR=$(new_test_workspace)
build_posix "$USER_BUILD_DIR" -DFROTH_USER_PROGRAM="$USER_PROGRAM_PATH"
FROTH_BINARY="$USER_BUILD_DIR/Froth"

FROTH_RUN_DIR=$(new_test_workspace)
run_froth ''
assert_contains 'USER-PROGRAM'

FROTH_RUN_DIR=$(new_test_workspace)
run_froth ': autorun "SNAPSHOT" s.emit cr ; save'
assert_contains 'USER-PROGRAM'

run_froth ''
assert_contains 'SNAPSHOT'
assert_not_contains 'USER-PROGRAM'
