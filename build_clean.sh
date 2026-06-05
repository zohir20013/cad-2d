#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

rm -rf "$BUILD_DIR"

if command -v ninja >/dev/null 2>&1; then
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
else
  cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi

cmake --build "$BUILD_DIR" --parallel "$JOBS"
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" "$BUILD_DIR/DWGViewerAdvanced"
