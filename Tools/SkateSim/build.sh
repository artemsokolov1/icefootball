#!/usr/bin/env bash
# Builds the standalone skating-core test runner. Warnings mirror Unreal's strictness
# (shadowing is an error in UE5 modules).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source/IceFootball/Skate"
CXX="${CXX:-g++}"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror \
  -I"$HERE/stubs" -I"$SRC/Core" -I"$SRC/Tests" \
  "$SRC/Core/SkateInput.cpp" "$SRC/Core/SkateModel.cpp" "$SRC/Core/SkateBallControl.cpp" \
  "$SRC/Core/SkateTuningPresets.cpp" "$SRC/Tests/SkateCoreTests.cpp" "$SRC/Tests/SkateCourseTests.cpp" "$SRC/Tests/SkatePoseTests.cpp" "$SRC/Core/SkatePose.cpp" "$HERE/main.cpp" \
  -o "$HERE/skatesim"
echo "built $HERE/skatesim"
