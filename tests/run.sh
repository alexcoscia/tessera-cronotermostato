#!/bin/sh
# Build and run the logic tests on this computer (no ESPHome, no LVGL). Usage: tests/run.sh [extra g++ flags]
set -eu
cd "$(dirname "$0")"
out="${TMPDIR:-/tmp}/cronotermostato_test_logic.$$"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -g "$@" test_logic.cpp -o "$out"
status=0
"$out" || status=$?
rm -f "$out"
exit $status
