#!/usr/bin/env bash
# perfproxy.sh NAME CPU SLICE.pre [ENV=VAL ...]: proxy.sh's transformer-only run under perf stat
# (instructions, cycles, task-clock), pinned to CPU. Prints loss + counters. Env BIN as proxy.sh.
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; R=$REPO/build/refs/fx2-cmix-transformer-v1
name=$1; cpu=$2; pre=$(realpath "$3"); shift 3
d=$REPO/build/exp/E10/perf/$name; rm -rf "$d"; mkdir -p "$d"; cd "$d"
env "$@" perf stat -e instructions,cycles,task-clock -o perf.txt taskset -c "$cpu" "${BIN:-$REPO/build/exp/E10/cmix_e10}" \
  -c "$R/tree/dictionary/english.dic" "$pre" out --transformer "$R/tree/models/6m-q4-fp32.tfwc2" --transformer-only > log 2> err \
  || { echo "FAIL $name"; tail -c 500 err; }
rm -f ppm.temp out progress.log
echo "== $name $* :: $(grep -E 'loss' log | tail -1) :: $(awk '/instructions|cycles|task-clock/{printf "%s %s  ", $1, $2}' perf.txt)"
