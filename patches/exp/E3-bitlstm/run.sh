#!/usr/bin/env bash
# run.sh <binary-name> <slice> <notes>: slot + bench a build/exp/E3/bin/cmix-<binary-name> in slice mode.
# BITL_* env passes through to both directions. Example (best 1M/10M setting):
#   BITL_LR_E6=3000 BITL_ACCUM=1 patches/exp/E3-bitlstm/run.sh bitl enwik9_10M "agg16"
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
export FX2_BIN="$REPO/build/exp/E3/bin/cmix-$1"
P=patches/fx2-cmix-transformer
scripts/slot.sh scripts/bench.sh --method "fx2T+E3-$1" --slice "data/slices/$2" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" \
  --decomp-size 3601651 --notes "$3; timing contended"
