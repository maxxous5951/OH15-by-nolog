#!/bin/sh
# Builds and runs the tests of the OpenHoldem stats database on Linux / macOS.
# Needs only g++ (SQLite is compiled from ../sqlite/sqlite3.c).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/oh_statsdb_tests"
mkdir -p "$OUT"
if [ ! -f "$OUT/sqlite3.o" ]; then
  gcc -O1 -c -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION ../sqlite/sqlite3.c -o "$OUT/sqlite3.o"
fi
g++ -std=c++14 -O1 -Wall -Wextra -Wno-unused-parameter \
  -I../OpenHoldem -I../sqlite \
  test_statsdb.cpp ../OpenHoldem/StatsDb.cpp "$OUT/sqlite3.o" \
  -lpthread -ldl -o "$OUT/test_statsdb"
"$OUT/test_statsdb" ../PokerTracker_Query_Definitions/PokerTracker_Queries_Version_4.h
