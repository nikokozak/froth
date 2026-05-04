#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
TEST_RUNNER_BIN=${FROTHY_TEST_RUNNER_BIN:-${FROTH_TEST_RUNNER_BIN:-}}
RUN_LIVE_CONTROLS=0
PORT=

if [ -d /opt/homebrew/bin ]; then
  PATH=/opt/homebrew/bin:$PATH
  export PATH
fi

usage() {
  echo "usage: $0 [--live-controls] <PORT>" >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --live-controls)
      RUN_LIVE_CONTROLS=1
      shift
      ;;
    --skip-live-controls)
      # Backward-compatible no-op now that non-interactive checks are default.
      RUN_LIVE_CONTROLS=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      usage
      exit 2
      ;;
    *)
      PORT=$1
      shift
      ;;
  esac
done

if [ -z "$PORT" ] || [ "$#" -ne 0 ]; then
  usage
  exit 2
fi

if [ ! -e "$PORT" ]; then
  echo "error: serial port is missing: $PORT" >&2
  exit 1
fi

set -- proof-workshop-v4 --port "$PORT"
if [ "$RUN_LIVE_CONTROLS" -eq 1 ]; then
  set -- "$@" --live-controls
fi

if [ -n "${TEST_RUNNER_BIN:-}" ] && [ -x "$TEST_RUNNER_BIN" ]; then
  exec "$TEST_RUNNER_BIN" "$@"
fi

if ! command -v go >/dev/null 2>&1; then
  echo "error: go is required for Frothy proof helpers" >&2
  exit 1
fi

GOCACHE_DIR=${GOCACHE:-$ROOT_DIR/.cache/go-build}
mkdir -p "$GOCACHE_DIR"
cd "$ROOT_DIR/tools/cli"
exec env GOCACHE="$GOCACHE_DIR" go run ./cmd/test-runner "$@"
