#!/usr/bin/env bash
# dump.sh NAME SLICE.pre: transformer-only run of the A3 dump build with the E2 logit head (default
# rates); writes build/exp/A3/dump/NAME.bin (int32 y, float xn[192], float raw_logits[205] per token)
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; R=$REPO/build/refs/fx2-cmix-transformer-v1
name=$1; pre=$2
d=$REPO/build/exp/A3/dump/run_$name; rm -rf $d; mkdir -p $d; cd $d
env E2_LOGIT=1 A3_DUMP=$REPO/build/exp/A3/dump/$name.bin /usr/bin/time -f "wall %e s rss %M kB" \
  $REPO/build/exp/A3/cmix_dump -c $R/tree/dictionary/english.dic $pre out --transformer $R/tree/models/6m-q4-fp32.tfwc2 --transformer-only > log 2> err
rm -f ppm.temp out progress.log
tail -3 log; tail -c 200 err; ls -l $REPO/build/exp/A3/dump/$name.bin
