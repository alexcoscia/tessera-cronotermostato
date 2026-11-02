#!/bin/bash
# Runs the touch flow of tools/bench_flow.py on one board of the bench. Usage: tools/bench_flow.sh <board> [out-folder]
# TESSERA_PLUGINS: the tessera-plugins checkout (default ../tessera-plugins). The pictures land in <out>/<board>/flow-*.png
# (default .preview-flow), the log in <out>/<board>/flow.log and the checks on the standard output.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(dirname "$here")"
plugins="${TESSERA_PLUGINS:-$repo/../tessera-plugins}"
plugins="$(cd "$plugins" && pwd)"
cache="$plugins/tools/.cache"
board="${1:?usage: tools/bench_flow.sh <board> [out-folder]}"
out="${2:-$repo/.preview-flow}"
export TESSERA_PLUGINS="$plugins"
export ESPHOME="${ESPHOME:-$cache/preview-venv/bin/esphome}"
export TESSERA_TREE="${TESSERA_TREE:-$cache/tessera-main}"
export TESSERA_BENCH_WORK="${TESSERA_BENCH_WORK:-$cache/bench-build}"
export RENDER_PORT_BASE="${RENDER_PORT_BASE:-7481}"
exec timeout 1500 "$cache/preview-venv/bin/python" "$here/bench_flow.py" "$repo" --board "$board" --out "$out/$board" --work "$TESSERA_BENCH_WORK"
