#!/bin/bash
# T1: observation-only cost-logged record runs on interwiki proxies build/exp/T1/iwp_<name>.pre (fast lane), no archive
# kept. Usage: scripts/analysis/t1_costlogs.sh 5k 120k ...  -> build/exp/T1/run_<name>/cost_iwp_<name>.bin
set -e
cd "$(dirname "${BASH_SOURCE[0]}")/../.."; REPO=$PWD
R=$PWD/build/refs/fx2-cmix-transformer-v1; D=$R/tree/dictionary/english.dic; W=$R/tree/models/6m-q4-fp32.tfwc2
for p in "$@"; do
  mkdir -p build/exp/T1/run_$p && cd build/exp/T1/run_$p
  ../../../../scripts/slot.sh --fast /usr/bin/time -f "wall %e s rss %M kB" env FX2_COSTLOG=$PWD/cost_iwp_$p.bin \
    ../../../../build/exp/A1/cmix_costlog -c $D ../iwp_$p.pre iwp_$p.cmix --transformer $W > log.txt 2>&1
  ls -l iwp_$p.cmix >> log.txt; rm -f iwp_$p.cmix
  cd "$REPO"
done
echo done
