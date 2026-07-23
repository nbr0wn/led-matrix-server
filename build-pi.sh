#!/usr/bin/env bash
# Cross-compile led-matrix-server (and the rgbmatrix library) for a 64-bit
# Raspberry Pi OS (aarch64) from an x86_64 host.
#
# The aarch64 toolchain is downloaded automatically into toolchain/ by
# `make cross` on first use. Requires: curl, tar (with xz support).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo ">> Cleaning previous build artifacts..."
make -C "$DIR" distclean >/dev/null 2>&1 || true

echo ">> Cross-compiling for aarch64 (toolchain auto-fetched into toolchain/)..."
make -C "$DIR" cross -j"$(nproc)"

echo ">> Done. Verifying binary architecture:"
if command -v file >/dev/null 2>&1; then
  file "$DIR/led-matrix-server"
else
  readelf -h "$DIR/led-matrix-server" | grep -E 'Class|Machine'
fi
