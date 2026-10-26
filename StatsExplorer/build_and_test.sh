#!/bin/sh
# Builds and runs the headless test of the explorer (needs only g++, gcc and the sources of Dear ImGui).
# usage: build_and_test.sh [imgui-dir]    (default: $IMGUI_DIR, else /tmp/claude-0/imgui)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
IMGUI="${1:-${IMGUI_DIR:-/tmp/claude-0/imgui}}"
OUT="${TMPDIR:-/tmp}/explorer_build"
mkdir -p "$OUT"
gcc -O1 -c "$ROOT/sqlite/sqlite3.c" -o "$OUT/sqlite3.o" -DSQLITE_THREADSAFE=1
for f in imgui imgui_draw imgui_tables imgui_widgets; do
  g++ -std=c++14 -O1 -c "$IMGUI/$f.cpp" -I"$IMGUI" -o "$OUT/$f.o"
done
g++ -std=c++14 -O1 -Wall -Wextra -o "$OUT/headless_render" \
  "$HERE/tests/headless_render.cpp" "$HERE/src/explorer_model.cpp" "$HERE/src/explorer_ui.cpp" \
  "$HERE/src/demo_data.cpp" "$ROOT/OpenHoldem/StatsDb.cpp" \
  "$OUT/sqlite3.o" "$OUT/imgui.o" "$OUT/imgui_draw.o" "$OUT/imgui_tables.o" "$OUT/imgui_widgets.o" \
  -I"$HERE/src" -I"$ROOT/OpenHoldem" -I"$ROOT/sqlite" -I"$IMGUI" -lpthread
"$OUT/headless_render" "$IMGUI" "$OUT"
