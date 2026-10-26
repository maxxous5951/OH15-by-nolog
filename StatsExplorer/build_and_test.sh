#!/bin/sh
# Builds and runs the tests of the explorer (needs only g++, gcc and the sources of Dear ImGui).
# usage: build_and_test.sh [imgui-dir]    (default: $IMGUI_DIR, else /tmp/claude-0/imgui)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
IMGUI="${1:-${IMGUI_DIR:-/tmp/claude-0/imgui}}"
OUT="${TMPDIR:-/tmp}/explorer_build"
mkdir -p "$OUT"
gcc -O1 -c "$ROOT/sqlite/sqlite3.c" -o "$OUT/sqlite3.o" -DSQLITE_THREADSAFE=1
for f in imgui imgui_draw imgui_tables imgui_widgets; do
  [ -f "$OUT/$f.o" ] || g++ -std=c++17 -O1 -c "$IMGUI/$f.cpp" -I"$IMGUI" -o "$OUT/$f.o"
done
FLAGS="-std=c++17 -O1 -Wall -Wextra -I$HERE/src -I$ROOT/OpenHoldem -I$ROOT/sqlite -I$IMGUI"
CORE="$HERE/src/explorer_model.cpp $HERE/src/demo_data.cpp $HERE/src/reports.cpp $HERE/src/equity.cpp $HERE/src/hh_import.cpp $HERE/src/export.cpp $ROOT/OpenHoldem/StatsDb.cpp $OUT/sqlite3.o"
g++ $FLAGS -o "$OUT/logic_tests" "$HERE/tests/logic_tests.cpp" $CORE -lpthread
IMGUI_OBJ="$OUT/imgui.o $OUT/imgui_draw.o $OUT/imgui_tables.o $OUT/imgui_widgets.o"
g++ $FLAGS -o "$OUT/headless_render" "$HERE/tests/headless_render.cpp" "$HERE/src/explorer_ui.cpp" "$HERE/src/ui_panels.cpp" $CORE $IMGUI_OBJ -lpthread
"$OUT/logic_tests" "$OUT"
"$OUT/headless_render" "$IMGUI" "$OUT"
