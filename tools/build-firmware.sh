#!/usr/bin/env bash
# Reproducible firmware build wrapper.
#
# Usage:
#   tools/build-firmware.sh            # build to $HOME/esp-build/buddy
#   BUILD_DIR=/path/to/build tools/build-firmware.sh
#   tools/build-firmware.sh --check    # only check the toolchain, no build
#
# The project is pinned to ESP-IDF v5.5.4 (see docs/reproducible-build.md).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/esp-build/buddy}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

if [ ! -d "$IDF_PATH" ]; then
    echo "ERROR: ESP-IDF not found at $IDF_PATH" >&2
    echo "Set IDF_PATH or install v5.5.4; see docs/reproducible-build.md" >&2
    exit 2
fi

VERSION=""
if [ -f "$IDF_PATH/tools/cmake/version.cmake" ]; then
    MAJOR="$(sed -n 's/^set(IDF_VERSION_MAJOR \([0-9][0-9]*\))/\1/p' "$IDF_PATH/tools/cmake/version.cmake" | head -1)"
    MINOR="$(sed -n 's/^set(IDF_VERSION_MINOR \([0-9][0-9]*\))/\1/p' "$IDF_PATH/tools/cmake/version.cmake" | head -1)"
    PATCH="$(sed -n 's/^set(IDF_VERSION_PATCH \([0-9][0-9]*\))/\1/p' "$IDF_PATH/tools/cmake/version.cmake" | head -1)"
    [ -n "$MAJOR" ] && [ -n "$MINOR" ] && [ -n "$PATCH" ] && VERSION="v$MAJOR.$MINOR.$PATCH"
fi
[ -z "$VERSION" ] && VERSION="$(git -C "$IDF_PATH" describe --tags --always 2>/dev/null || true)"
if [ -z "$VERSION" ] && [ -f "$IDF_PATH/version.txt" ]; then
    VERSION="$(cat "$IDF_PATH/version.txt")"
fi
echo "ESP-IDF: $VERSION ($IDF_PATH)"

case "$VERSION" in
    v5.5.4*|5.5.4*) ;;
    *)
        echo "ERROR: expected ESP-IDF v5.5.4, found '$VERSION'" >&2
        echo "Use the pinned container: espressif/idf:v5.5.4" >&2
        exit 3
        ;;
esac

if [ "$CHECK_ONLY" -eq 1 ]; then
    echo "toolchain check: PASS"
    exit 0
fi

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null
cd "$ROOT/firmware"
idf.py -B "$BUILD_DIR" build
echo "build: $BUILD_DIR"
