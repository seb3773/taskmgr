#!/bin/bash
# build_and_run_asan.sh - Build and run taskmgr with AddressSanitizer (ASan)

set -e

# Working directory must be the project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== [1/3] Preparing ASan build directory ==="
mkdir -p build_asan
cd build_asan

echo "=== [2/3] Configuring with CMake (-DTASKMGR_ASAN=ON) ==="
cmake -DTASKMGR_ASAN=ON ..

echo "=== [3/3] Compiling taskmgr ==="
make -j$(nproc)

# Check for --build-only parameter
if [ "$1" = "--build-only" ]; then
    echo "=== Build completed successfully (build-only mode). ==="
    exit 0
fi

echo "=== Running taskmgr with AddressSanitizer ==="
echo "Note: detect_odr_violation=0 is set to bypass duplicate symbols warnings between shared libraries."
export ASAN_OPTIONS="detect_leaks=1:detect_odr_violation=0:halt_on_error=0:log_path=asan.log"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:log_path=asan.log"

# Preload libasan to ensure it comes first in the library list
LIBASAN_PATH=$(gcc -print-file-name=libasan.so 2>/dev/null || true)
if [ -n "$LIBASAN_PATH" ] && [ -f "$LIBASAN_PATH" ] && [ "$LIBASAN_PATH" != "libasan.so" ]; then
    echo "Preloading ASan runtime: $LIBASAN_PATH"
    export LD_PRELOAD="$LIBASAN_PATH"
fi

./taskmgr
