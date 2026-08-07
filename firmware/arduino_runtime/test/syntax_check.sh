#!/usr/bin/env bash
# Host-side syntax check for the Cheeko Gotchi Arduino runtime.
#
# There is no Arduino toolchain on the dev box, so this compiles the runtime
# with plain g++ against test/arduino_stubs.h (minimal no-op fakes of every
# Arduino / ESP-IDF API the runtime touches). It catches C++ errors — wrong
# signatures vs cheeko.h, missing includes, type errors — but proves nothing
# about hardware behaviour. -std=c++14 matches the oldest compiler in play
# (MinGW 6.3 on the dev box); avoid C++17-only features in the runtime.
#
# Run from anywhere:   bash firmware/arduino_runtime/test/syntax_check.sh

set -e
cd "$(dirname "$0")/.."          # -> firmware/arduino_runtime
REPO="$(cd ../.. && pwd)"        # -> repo root

FLAGS="-std=c++14 -fsyntax-only -DCHEEKO_SYNTAX_CHECK"
INC="-I $REPO/sdk/include -I $REPO/sdk/runtime -I $REPO/firmware/arduino_runtime"

# 1. The runtime itself (the canonical check from the task spec).
g++ $FLAGS $INC -include test/arduino_stubs.h cheeko_runtime.cpp cheeko_hw.cpp

# 2. The sketch template (g++ refuses .ino.tpl, so check a .cpp copy).
cp CheekoRuntime.ino.tpl test/_ino_check.cpp
g++ $FLAGS $INC -include test/arduino_stubs.h test/_ino_check.cpp
rm -f test/_ino_check.cpp

# 3. A real example app against the same headers, proving the app contract.
g++ $FLAGS $INC -include test/arduino_stubs.h "$REPO/examples/animated_face/src/app.cc"

echo "syntax check OK"
