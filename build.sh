#!/usr/bin/env bash
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SRC_ROOT/build"

need_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing tool: $1" 1>&2
		return 1
	fi
	return 0
}

missing=0
need_cmd cmake || missing=1
need_cmd pkg-config || missing=1
need_cmd bash || missing=1

if test "$missing" -ne 0; then
	echo "" 1>&2
	echo "Some dependencies are missing." 1>&2
	echo "Install suggestions (examples):" 1>&2
	echo "- Debian/Ubuntu: sudo apt-get install cmake pkg-config" 1>&2
	exit 1
fi

mkdir -p -- "$BUILD_DIR"

# Configure in Release mode, turning off ASan/UBSan instrumentation
cmake -S "$SRC_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DTASKMGR_ASAN=OFF
cmake --build "$BUILD_DIR" -j"$(nproc)"

BIN_PATH="$BUILD_DIR/taskmgr"
if test -x "$BIN_PATH"; then
	if command -v sstrip >/dev/null 2>&1; then
		echo "info: stripping binary with sstrip"
		sstrip "$BIN_PATH" >/dev/null 2>&1 || true
	else
		echo "info: sstrip not found, using standard strip"
		strip --strip-all "$BIN_PATH" >/dev/null 2>&1 || true
	fi
fi
