#!/usr/bin/env bash
#
# LiteOTA Native Tests — one-shot runner
#
# Kitronic — https://github.com/kitronic/esp-lib-lite-ota
#
# Usage:  ./run.sh
#
# Compiles test_native.cpp with g++ and runs it.
# No make, no PlatformIO required.

set -euo pipefail

cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow)
BIN="test_runner"

echo "▸ Compiler: $CXX"
echo "▸ Flags:    ${CXXFLAGS[*]}"
echo

"$CXX" "${CXXFLAGS[@]}" -o "$BIN" test_native.cpp

echo "▸ Running tests..."
echo

"./$BIN"