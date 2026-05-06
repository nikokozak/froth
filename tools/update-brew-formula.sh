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
OUTPUT=${3:-Formula/froth.rb}

mkdir -p "$(dirname "$OUTPUT")"
TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/froth-formula.XXXXXX")
trap 'rm -f "$TMP_FILE"' EXIT INT TERM

# Generate a Froth formula that builds the public CLI executable from the
# tagged source archive. This keeps `brew install froth` tied to the public
# release tag without requiring per-platform URL selection in the formula.
cat >"$TMP_FILE" <<EOF
class Froth < Formula
  desc "Live lexical language for programmable devices"
  homepage "https://frothlang.org"
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
      system "go", "build", "-o", bin/"froth", "."
    end
  end

  test do
    output = shell_output("#{bin}/froth --version")
    assert_match "froth ", output
  end
end
EOF

mv "$TMP_FILE" "$OUTPUT"
