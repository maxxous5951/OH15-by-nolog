#!/usr/bin/env bash
# Test différentiel : arbre syntaxique de chaque fonction, C++ (ohdump) contre Python (SpinGoSimulator/ohlang.py).
# Usage : tools/diff_test.sh <build>/ohdump  fichier1.ohf fichier2.txt ...
# Les listes sont exclues : elles suivent volontairement OpenHoldem (COHScriptList), pas le Python.
set -euo pipefail
dump="$1"; shift
here="$(cd "$(dirname "$0")" && pwd)"
printf '%s\0' "$@" | xargs -0 "$dump"                    | grep -v -P '\tLIST ' | grep -v -P '^list' > /tmp/cpp.dump
printf '%s\0' "$@" | xargs -0 python3 "$here/ohdump.py"  | grep -v -P '\tLIST ' | grep -v -P '^list' > /tmp/py.dump
n=$(wc -l < /tmp/cpp.dump)
d=$(diff /tmp/cpp.dump /tmp/py.dump | grep -c '^[<>]' || true)
echo "$n lignes comparées, $d écart(s)"
[ "$d" -eq 0 ]
