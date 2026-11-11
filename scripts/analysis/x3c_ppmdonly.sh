#!/usr/bin/env bash
# X3c: run the real PPMD component alone (cmix --ppmd-only, order 25 / 14 GB, as in the stack) over a slicetool-pre
# stream and keep its progress.log (cumulative coded bytes at 0.01% steps) and total size.
#   x3c_ppmdonly.sh <name> <in.pre>   -> build/exp/X3c/ppmd/<name>.{progress.log,size}
# The run's cwd is on /dev/shm ON PURPOSE: PPMD keeps its model in a MAP_SHARED file (ppm.temp) and on btrfs several
# such runs dirty ~1 GB/s of writeback and stall every other job on the box (seen 2026-11-11). A 100 MB sample needs
# about 1.2 GB there; do not use this for full enwik9 (the model grows to many GB).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
R="$REPO/build/refs/fx2-cmix-transformer-v1"; O="$REPO/build/exp/X3c/ppmd"; mkdir -p "$O"
n="$1"; in="$(realpath "$2")"; d="/dev/shm/x3c_ppmd_$n"; rm -rf "$d"; mkdir -p "$d"; cd "$d"
"$REPO/build/exp/C4/bin/cmix" -c "$R/tree/dictionary/english.dic" "$in" out.bin --ppmd-only > /dev/null 2>&1
stat -c %s out.bin > "$O/$n.size"; cp progress.log "$O/$n.progress.log"; cd /; rm -rf "$d"
echo "$n $(cat "$O/$n.size")"
