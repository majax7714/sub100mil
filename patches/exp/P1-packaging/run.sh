#!/usr/bin/env bash
# run.sh <bin> <name> <comp9-bytes> <notes>: fast-lane bench.sh round trip of a (packed) C3 binary on enwik9_1M
# with C3's runtime env (patches/exp/C3-combo/run.sh); --keep, so the archive can be cmp'd against the
# unpacked control. decomp_size = the comp9 that this exe gives (exe + comp_dict + comp_order + weights + header).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
export FX2_BIN=$(realpath "$1")
export E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1 E2_A0=0.25 E2_AF=0.05
export E12_DICT="$REPO/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic" E12_LR=0.004 X2_DICT="$REPO/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic"
P=patches/fx2-cmix-transformer
scripts/slot.sh --fast scripts/bench.sh --keep --method "fx2T+C3-P1-$2" --slice data/slices/${SLICE:-enwik9_1M} \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" --decomp-size "$3" \
  --notes "P1 packaging of the C3 binary ($(stat -c %s "$FX2_BIN") B exe): $4; C3 env; timing contended"
