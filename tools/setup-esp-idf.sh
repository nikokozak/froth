#!/usr/bin/env bash
#
# Fetch and install ESP-IDF for Froth development.
# Installs to ~/.froth/sdk/esp-idf/ by default.
#
# Usage:
#   ./tools/setup-esp-idf.sh           # install (skip if already present)
#   ./tools/setup-esp-idf.sh --force   # reinstall from scratch

set -e

ESP_IDF_VERSION="v5.4"
FROTH_SDK_DIR="$HOME/.froth/sdk"
IDF_INSTALL_DIR="$FROTH_SDK_DIR/esp-idf"

FORCE=0
if [ "$1" = "--force" ]; then
  FORCE=1
fi

if ! command -v git &> /dev/null; then
  echo "error: git is not installed."
  exit 1
fi

if [ -d "$IDF_INSTALL_DIR" ]; then
  if [ "$FORCE" -eq 0 ]; then
    echo "ESP-IDF already installed at $IDF_INSTALL_DIR"
    echo "Run with --force to reinstall."
    echo ""
    echo "To activate:  source $IDF_INSTALL_DIR/export.sh"
    exit 0
  fi
  echo "Removing existing install at $IDF_INSTALL_DIR..."
  rm -rf "$IDF_INSTALL_DIR"
fi

mkdir -p "$FROTH_SDK_DIR"

echo "Cloning ESP-IDF $ESP_IDF_VERSION..."
git clone \
  --branch "$ESP_IDF_VERSION" \
  --recursive \
  --depth 1 \
  --shallow-submodules \
  https://github.com/espressif/esp-idf.git \
  "$IDF_INSTALL_DIR"

echo "Installing ESP-IDF toolchain (esp32 target only)..."
"$IDF_INSTALL_DIR/install.sh" esp32

echo ""
echo "Done. To activate ESP-IDF in your current shell, run:"
echo ""
echo "  source $IDF_INSTALL_DIR/export.sh"
echo ""
echo "Then build Froth for ESP32:"
echo ""
echo "  cd targets/esp-idf"
echo "  idf.py set-target esp32"
echo "  idf.py build"
echo ""
