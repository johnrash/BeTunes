#!/bin/bash
# Builds and runs the unit tests (any system with g++; on Linux also with
# AddressSanitizer and UndefinedBehaviorSanitizer).
#   bash tests/run-unit-tests.sh
set -e
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/betunes-unit-tests
FLAGS="-std=c++17 -Wall -Wextra -g"
if [ "$(uname)" != "Haiku" ]; then
	FLAGS="$FLAGS -fsanitize=address,undefined"
fi
g++ $FLAGS -Isrc tests/unit-tests.cpp src/TextUtils.cpp src/PlayQueue.cpp \
	src/Equalizer.cpp src/Id3Tag.cpp \
	-o "$OUT"
"$OUT"
