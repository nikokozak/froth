#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTH_BINARY:-${FROTHY_BINARY:-$ROOT_DIR/build/froth}}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/frothy-m8-inspect.XXXXXX")"

cleanup() {
  rm -rf "$WORK_DIR"
}

trap cleanup EXIT HUP INT TERM

if [ ! -x "$BINARY" ]; then
  echo "error: $BINARY is missing; run cmake -S . -B build && cmake --build build" >&2
  exit 1
fi

run_transcript() {
  (
    cd "$WORK_DIR"
    printf '%s\n' "$@" | "$BINARY"
  )
}

require_contains() {
  transcript=$1
  needle=$2
  if ! printf '%s\n' "$transcript" | grep -F "$needle" >/dev/null; then
    echo "error: expected transcript to contain: $needle" >&2
    exit 1
  fi
}

require_not_contains() {
  transcript=$1
  needle=$2
  if printf '%s\n' "$transcript" | grep -F "$needle" >/dev/null; then
    echo "error: transcript unexpectedly contained: $needle" >&2
    exit 1
  fi
}

count_occurrences() {
  transcript=$1
  needle=$2
  printf '%s\n' "$transcript" |
    awk -v needle="$needle" 'index($0, needle) { count++ } END { print count + 0 }'
}

require_not_contains_line() {
  transcript=$1
  needle=$2
  if printf '%s\n' "$transcript" | grep -Fx "$needle" >/dev/null; then
    echo "error: transcript unexpectedly contained line: $needle" >&2
    exit 1
  fi
}

HELP_TRANSCRIPT="$(
  run_transcript \
    'help' \
    'quit'
)"
printf '%s\n' "$HELP_TRANSCRIPT"
require_contains "$HELP_TRANSCRIPT" 'words'
require_contains "$HELP_TRANSCRIPT" 'show @name'
require_contains "$HELP_TRANSCRIPT" 'see @name'
require_contains "$HELP_TRANSCRIPT" 'core @name'
require_contains "$HELP_TRANSCRIPT" 'info @name'
require_contains "$HELP_TRANSCRIPT" 'remember'
require_contains "$HELP_TRANSCRIPT" 'save'
require_contains "$HELP_TRANSCRIPT" 'restore'
require_contains "$HELP_TRANSCRIPT" 'dangerous.wipe'
require_contains "$HELP_TRANSCRIPT" '.control'
require_contains "$HELP_TRANSCRIPT" 'quit'
require_contains "$HELP_TRANSCRIPT" 'exit'

WORDS_TRANSCRIPT="$(
  run_transcript \
    'to inc with x [ x + 1 ]' \
    'words' \
    'quit'
)"
printf '%s\n' "$WORDS_TRANSCRIPT"
require_contains "$WORDS_TRANSCRIPT" 'save'
require_contains "$WORDS_TRANSCRIPT" 'restore'
require_contains "$WORDS_TRANSCRIPT" 'dangerous.wipe'
require_contains "$WORDS_TRANSCRIPT" 'words'
require_contains "$WORDS_TRANSCRIPT" 'see'
require_contains "$WORDS_TRANSCRIPT" 'core'
require_contains "$WORDS_TRANSCRIPT" 'slotInfo'
require_contains "$WORDS_TRANSCRIPT" 'inc'
require_not_contains_line "$WORDS_TRANSCRIPT" 'boot'
require_not_contains "$WORDS_TRANSCRIPT" 'froth> nil'

BOOT_WORDS_TRANSCRIPT="$(
  run_transcript \
    'to boot [ nil ]' \
    'words' \
    'quit'
)"
printf '%s\n' "$BOOT_WORDS_TRANSCRIPT"
require_contains "$BOOT_WORDS_TRANSCRIPT" 'boot'
require_not_contains "$BOOT_WORDS_TRANSCRIPT" 'froth> nil'

BASE_TRANSCRIPT="$(
  run_transcript \
    'show @save' \
    'core @save' \
    'info @save' \
    'quit'
)"
printf '%s\n' "$BASE_TRANSCRIPT"
require_contains "$BASE_TRANSCRIPT" 'save'
require_contains "$BASE_TRANSCRIPT" '  slot: base'
require_contains "$BASE_TRANSCRIPT" '  kind: native'
require_contains "$BASE_TRANSCRIPT" '  call: 0 -> 1'
require_contains "$BASE_TRANSCRIPT" '  owner: runtime builtin'
require_contains "$BASE_TRANSCRIPT" '  persistence: not saved'
require_contains "$BASE_TRANSCRIPT" '  help: Save the current overlay snapshot.'
require_contains "$BASE_TRANSCRIPT" '  see: <native save/0>'
require_contains "$BASE_TRANSCRIPT" '  core: <native save/0>'
require_not_contains "$BASE_TRANSCRIPT" 'eval error ('
require_not_contains "$BASE_TRANSCRIPT" 'parse error ('

INSPECT_TRANSCRIPT="$(
  run_transcript \
    'to inc with x [ x + 1 ]' \
    'alias is inc' \
    'show @alias' \
    'core @alias' \
    'info @alias' \
    'quit'
)"
printf '%s\n' "$INSPECT_TRANSCRIPT"
require_contains "$INSPECT_TRANSCRIPT" 'alias'
require_contains "$INSPECT_TRANSCRIPT" '  slot: overlay'
require_contains "$INSPECT_TRANSCRIPT" '  kind: code'
require_contains "$INSPECT_TRANSCRIPT" '  call: 1 -> 1'
require_contains "$INSPECT_TRANSCRIPT" '  owner: overlay image'
require_contains "$INSPECT_TRANSCRIPT" '  persistence: saved in snapshot'
require_contains "$INSPECT_TRANSCRIPT" '  see: to alias with arg0 [ arg0 + 1 ]'
require_contains "$INSPECT_TRANSCRIPT" '  core: (fn arity=1 locals=1 (seq (call (builtin "+") (read-local 0) (lit 1))))'
require_not_contains "$INSPECT_TRANSCRIPT" 'eval error ('
require_not_contains "$INSPECT_TRANSCRIPT" 'parse error ('

REBIND_TRANSCRIPT="$(
  run_transcript \
    'see is fn [ 42 ]' \
    'info @see' \
    'see:' \
    'quit'
)"
printf '%s\n' "$REBIND_TRANSCRIPT"
require_contains "$REBIND_TRANSCRIPT" 'see'
require_contains "$REBIND_TRANSCRIPT" '  owner: overlay image'
require_contains "$REBIND_TRANSCRIPT" '  persistence: saved in snapshot'
require_contains "$REBIND_TRANSCRIPT" '42'
require_not_contains "$REBIND_TRANSCRIPT" 'eval error ('
require_not_contains "$REBIND_TRANSCRIPT" 'parse error ('

COMMAND_TRANSCRIPT="$(
  run_transcript \
    'to alias [ 42 ]' \
    'show @alias' \
    'core @alias' \
    'info @alias' \
    'note is "saved"' \
    'remember' \
    'note is "changed"' \
    'restore' \
    'note' \
    'dangerous.wipe' \
    'note' \
    'quit'
)"
printf '%s\n' "$COMMAND_TRANSCRIPT"
require_contains "$COMMAND_TRANSCRIPT" 'alias'
require_contains "$COMMAND_TRANSCRIPT" '  slot: overlay'
require_contains "$COMMAND_TRANSCRIPT" '  kind: code'
require_contains "$COMMAND_TRANSCRIPT" '  see: to alias [ 42 ]'
require_contains "$COMMAND_TRANSCRIPT" '  core: (fn arity=0 locals=0 (seq (lit 42)))'
require_contains "$COMMAND_TRANSCRIPT" '  persistence: saved in snapshot'
require_contains "$COMMAND_TRANSCRIPT" 'froth> "saved"'
require_contains "$COMMAND_TRANSCRIPT" 'eval error ('
require_not_contains "$COMMAND_TRANSCRIPT" 'froth> nil'

SIMPLE_CALL_TRANSCRIPT="$(
  run_transcript \
    'to add with a, b [ a + b ]' \
    'add: 4, 5' \
    'add: -1, 2' \
    'to tick.on [ 7 ]' \
    'tick.on:' \
    'quit'
)"
printf '%s\n' "$SIMPLE_CALL_TRANSCRIPT"
require_contains "$SIMPLE_CALL_TRANSCRIPT" 'froth> froth> 9'
require_contains "$SIMPLE_CALL_TRANSCRIPT" 'froth> 1'
require_contains "$SIMPLE_CALL_TRANSCRIPT" 'froth> froth> 7'
require_not_contains "$SIMPLE_CALL_TRANSCRIPT" 'parse error ('

CALLABLE_TRANSCRIPT="$(
  run_transcript \
    'note is "saved"' \
    'save' \
    'note is "changed"' \
    'restore' \
    'note' \
    'dangerous.wipe' \
    'note' \
    'show @save' \
    'quit'
)"
printf '%s\n' "$CALLABLE_TRANSCRIPT"
require_contains "$CALLABLE_TRANSCRIPT" '  see: <native save/0>'
require_contains "$CALLABLE_TRANSCRIPT" 'froth> "saved"'
require_contains "$CALLABLE_TRANSCRIPT" 'eval error ('
require_not_contains "$CALLABLE_TRANSCRIPT" 'froth> nil'
