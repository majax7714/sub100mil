#!/usr/bin/env bash
# perf_cmp.sh <out.tsv> <bin:weights>...: user-space instruction counts (perf stat -e instructions:u) of slice-mode
# compression of two 200 KB slices (enwik9 start, census mid-file) with C3's env, per binary. Run under slot.sh --fast.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT=$(realpath -m "$1"); shift
export E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1 E2_A0=0.25 E2_AF=0.05
export E12_DICT="$REPO/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic" E12_LR=0.004 X2_DICT="$E12_DICT"
W=$(mktemp -d -p "${BENCH_TMP:-/tmp}"); trap 'rm -rf "$W"' EXIT
for sl in e200k m200k; do
  for bw in "$@"; do
    bin=$(realpath "${bw%%:*}"); wt=$(realpath "${bw##*:}")
    cd "$W"; rm -f out.bin
    "$REPO/build/refs/fx2-cmix-transformer-v1/slicetool" pre "$REPO/build/exp/P1/w/$sl" slice.pre
    n=$(perf stat -x, -e instructions:u "$bin" -c "$E12_DICT" slice.pre out.bin --transformer "$wt" 2>&1 >/dev/null | grep instructions | cut -d, -f1)
    printf "%s\t%s\t%s\t%s\n" "$sl" "$bin" "$n" "$(stat -c %s out.bin)" >> "$OUT"
    rm -f ppm.temp progress.log
  done
done
