#!/usr/bin/env bash
# run.sh <name> <slice-name> <notes> [--fast]: slot-limited bench.sh round trip of build/exp/C3/bin/cmix in slice
# mode with the C3 runtime configuration (C2's plus X2_DICT): C1's (E2 logit head, E3 agg16 BITLSTM), E2 logit LR x0.5
# (E2_A0=0.25 E2_AF=0.05, A3's best fixed multiplier) and E12 (E12_DICT, E12_LR). Extra env passes through to BOTH
# directions; record it in <name>/<notes>. <slice-name> is a name in data/slices/.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
V=$1; S=$2; N=$3; LANE=(); [[ "${4-}" == --fast ]] && LANE=(--fast)
export FX2_BIN="$REPO/build/exp/C3/bin/cmix"
export E2_LOGIT="${E2_LOGIT-1}" BITL_LR_E6="${BITL_LR_E6-3000}" BITL_ACCUM="${BITL_ACCUM-1}"
export E2_A0="${E2_A0-0.25}" E2_AF="${E2_AF-0.05}"
export E12_DICT="${E12_DICT-$REPO/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic}" E12_LR="${E12_LR-0.004}"
export X2_DICT="${X2_DICT-$E12_DICT}"
P=patches/fx2-cmix-transformer
scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "fx2T+C3-$V" --slice "$( [[ $S == */* ]] && echo "$S" || echo "data/slices/$S")" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" \
  --notes "C3 combo: C2 + X2 timelaw [X2_DICT]; C2 = C1 (E1 gate3 + E2 logit [E2_LOGIT=$E2_LOGIT E2_A0=$E2_A0 E2_AF=$E2_AF] + E3 agg16 [BITL_LR_E6=$BITL_LR_E6 BITL_ACCUM=$BITL_ACCUM] + E9) + E12 [E12_LR=$E12_LR]; $N; timing contended"
