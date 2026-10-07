#!/usr/bin/env bash
# Builds the pose renderer and writes poses.svg (+ poses.png if a headless Chromium is available).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source/IceFootball/Skate"
OUT="${1:-$HERE/out}"
mkdir -p "$OUT"
"${CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror -I"$HERE/stubs" -I"$SRC/Core" \
  "$SRC/Core/SkatePose.cpp" "$SRC/Core/SkateTuningPresets.cpp" "$SRC/Core/SkateBallControl.cpp" "$HERE/pose_render.cpp" -o "$OUT/pose_render"
"$OUT/pose_render" > "$OUT/poses.svg"
CHROME="${CHROME:-/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell}"
if [ -x "$CHROME" ]; then
  SIZE=$(grep -o "width='[0-9]*' height='[0-9]*'" "$OUT/poses.svg" | head -1 | sed "s/width='\([0-9]*\)' height='\([0-9]*\)'/\1,\2/")
  "$CHROME" --no-sandbox --headless --disable-gpu --hide-scrollbars --window-size="$SIZE" --screenshot="$OUT/poses.png" "file://$OUT/poses.svg" >/dev/null 2>&1 || true
fi
echo "$OUT/poses.svg"
