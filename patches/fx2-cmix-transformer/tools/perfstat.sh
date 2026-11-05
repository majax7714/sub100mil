#!/usr/bin/env bash
# perf stat (topdown + cache-fill counters) on transformer-only, ppmd-only and full-model runs of
# fx2-cmix-transformer-v1 on a slice, plus perf record of the full model (symbol-level self time).
# Usage: perfstat.sh <slice> <outdir>    (wrap in flock: timing run). Needs perf (paranoid <= 2).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
R="$REPO/build/refs/fx2-cmix-transformer-v1"
CORE=${CORE:-2}
slice="$(realpath "$1")"; out="$(realpath -m "$2")"; mkdir -p "$out"; cd "$out"
"$R/slicetool" pre "$slice" s.pre 2>/dev/null
D="$R/tree/dictionary/english.dic"; W="$R/tree/models/6m-q4-fp32.tfwc2"
EV=cycles,instructions,ls_any_fills_from_sys.local_ccx,ls_any_fills_from_sys.dram_io_all,l2_pf_miss_l2_hit_l3.all
for cfg in "ppmd_only:--ppmd-only" "tf_only:--transformer $W --transformer-only" "full:--transformer $W"; do
  name="${cfg%%:*}"; flags="${cfg#*:}"
  # shellcheck disable=SC2086
  perf stat -o "stat_$name.txt" -M PipelineL1,backend_bound_group,all_l2_cache_misses -e "$EV" \
    taskset -c "$CORE" "$R/cmix_syms" -c "$D" s.pre o.bin $flags > "log_$name.txt" 2>&1
  rm -f ppm.temp progress.log o.bin
done
perf record -q -F 499 -o perf_full.data taskset -c "$CORE" "$R/cmix_syms" -c "$D" s.pre o.bin --transformer "$W" > log_rec.txt 2>&1
perf report -i perf_full.data --no-children --sort symbol --stdio 2>/dev/null | grep -v "^#" | grep -v "^$" | head -80 > report_full.txt
rm -f ppm.temp progress.log o.bin s.pre
