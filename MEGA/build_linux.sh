#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
echo "=== Building MEGA (1v1 Zero-Dep P2P Edition) for Linux ==="
echo "Note: Only CMake + C++20 compiler (+ OpenGL headers) needed. SDL3 is auto-fetched if absent."

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

if [ -f "$BUILD_DIR/mega_tests" ]; then
    echo "=== Running unit tests ==="
    "$BUILD_DIR/mega_tests"
fi

echo ""
echo "Build complete! Run the game with:"
echo "  ./$BUILD_DIR/mega"
