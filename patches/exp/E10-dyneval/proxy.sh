#!/usr/bin/env bash
# proxy.sh NAME SLICE.pre [ENV=VAL ...]: transformer-only run (PPMd prior + transformer, no CM, about
# 1.2 GB RSS); prints the transformer's own loss (nats/token), which E10 changes directly.
# SLICE.pre = build/refs/fx2-cmix-transformer-v1/slicetool pre <slice> <SLICE.pre>.
# Env: BIN (default build/exp/E10/cmix_e10). Not a result: confirm anything with scripts/bench.sh.
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; R=$REPO/build/refs/fx2-cmix-transformer-v1
name=$1; pre=$(realpath "$2"); shift 2
d=$REPO/build/exp/E10/proxy/run_$name; rm -rf "$d"; mkdir -p "$d"; cd "$d"
env "$@" /usr/bin/time -f "wall %e s rss %M kB" "${BIN:-$REPO/build/exp/E10/cmix_e10}" -c "$R/tree/dictionary/english.dic" \
  "$pre" out --transformer "$R/tree/models/6m-q4-fp32.tfwc2" --transformer-only > log 2> err || { echo "FAIL $name"; tail -c 500 err; }
rm -f ppm.temp out progress.log
echo "== $name $* :: $(grep -E 'loss' log | tail -2 | tr '\n' ' ') :: $(grep -oE 'wall.*' err)"
