#!/bin/sh
# Build and run the logic tests on this computer (no ESPHome, no LVGL). Usage: tests/run.sh [extra g++ flags]
set -eu
cd "$(dirname "$0")"
status=0
for t in test_logic test_edit test_pad; do
  out="${TMPDIR:-/tmp}/cronotermostato_$t.$$"
  g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -g "$@" $t.cpp -o "$out"
  rc=0
  "$out" || rc=$?
  rm -f "$out"
  [ $rc -eq 0 ] || status=$rc
done
exit $status
