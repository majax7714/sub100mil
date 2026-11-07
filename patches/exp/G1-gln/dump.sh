#!/usr/bin/env bash
# dump.sh <slice-name> <lane-flag or ''>: compress-only run of cmix_dump with C3 env, writes build/exp/G1/dump_<slice>.bin
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
S=$1; LANE=${2-}
W=$(mktemp -d -p ${BENCH_TMP:-/tmp})
export FX2_BIN="$REPO/build/exp/G1/bin/cmix_dump"
export E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1 E2_A0=0.25 E2_AF=0.05
export E12_DICT="$REPO/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic" E12_LR=0.004
export X2_DICT="$E12_DICT" G1_DUMP="$REPO/build/exp/G1/dump_$S.bin"
scripts/slot.sh $LANE /usr/bin/time -v patches/fx2-cmix-transformer/slice_cmd.sh comp data/slices/$S $W/comp.bin 2> "$REPO/build/exp/G1/dump_$S.time"
ls -l $W/comp.bin
rm -rf "$W"
