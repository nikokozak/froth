#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT_DIR/tools/release-common.sh"

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  printf 'usage: %s <version> <source-sha> [output]\n' "${0##*/}" >&2
  exit 1
fi

VERSION=$(normalize_version "$1")
SOURCE_SHA=$2
OUTPUT=${3:-Formula/frothy.rb}

mkdir -p "$(dirname "$OUTPUT")"
TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/frothy-formula.XXXXXX")
trap 'rm -f "$TMP_FILE"' EXIT INT TERM

# Generate a Frothy-branded formula that builds the Frothy-owned CLI executable
# from the tagged source archive. This keeps `brew install frothy` tied to the
# public release tag without requiring per-platform URL selection in the formula.
cat >"$TMP_FILE" <<EOF
class Frothy < Formula
  desc "Live lexical language for programmable devices"
  homepage "https://github.com/nikokozak/frothy"
  url "https://github.com/${RELEASE_REPO_SLUG}/archive/refs/tags/v${VERSION}.tar.gz"
  sha256 "${SOURCE_SHA}"
  license "MIT"

  head "https://github.com/${RELEASE_REPO_SLUG}.git", branch: "main"

  depends_on "go" => :build

  def install
    cd "tools/cli" do
      system "go", "run", "./internal/sdk/cmd/generate",
             "-repo", buildpath.to_s,
             "-out", "internal/sdk/generated"
      system "go", "build", "-o", bin/"frothy", "."
    end
  end

  test do
    output = shell_output("#{bin}/frothy --version")
    assert_match "frothy ", output
  end
end
EOF

mv "$TMP_FILE" "$OUTPUT"
