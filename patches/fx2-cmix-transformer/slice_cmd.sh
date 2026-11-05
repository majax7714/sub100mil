#!/usr/bin/env bash
# Round-trip wrapper for running fx2-cmix-transformer-v1 (built by build.sh) on a prefix slice of
# enwik9 through scripts/bench.sh:
#   slice_cmd.sh comp   <slice> <out>   = slicetool pre  -> cmix -c english.dic ... --transformer W
#   slice_cmd.sh decomp <in>    <out>   = cmix -d english.dic ... --transformer W -> slicetool post
# Runs inside the directory of <out> (bench.sh's work dir): cmix writes ppm.temp (sparse 14 GiB
# PPMD mmap), progress.log and removes ./.dict in its cwd.
# Env: FX2_BIN (default build/refs/fx2-cmix-transformer-v1/cmix).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
R="$REPO/build/refs/fx2-cmix-transformer-v1"
BIN="${FX2_BIN:-$R/cmix}"
DICT="$R/tree/dictionary/english.dic"
W="$R/tree/models/6m-q4-fp32.tfwc2"
mode="$1"; in="$(realpath "$2")"; out="$(realpath -m "$3")"
cd "$(dirname "$out")"
case "$mode" in
  comp)   "$R/slicetool" pre "$in" slice.pre
          "$BIN" -c "$DICT" slice.pre "$out" --transformer "$W" > comp.log
          rm -f slice.pre ;;
  decomp) "$BIN" -d "$DICT" "$in" slice.dec --transformer "$W" > decomp.log
          "$R/slicetool" post slice.dec "$out"
          rm -f slice.dec ;;
  *) echo "usage: slice_cmd.sh comp|decomp in out" >&2; exit 2 ;;
esac
rm -f ppm.temp progress.log
