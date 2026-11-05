#!/usr/bin/env bash
# run.sh <name> <slice-name> <notes> [--fast]: slot-limited bench.sh round trip of build/exp/C1/bin/cmix in slice
# mode with the C1 runtime configuration (E2 logit head, E3 agg16 BITLSTM). Extra env (e.g. E9_FXCM_FINAL=0,
# BITL=0, E2_LOGIT=0) passes through to BOTH directions; record it in <name>/<notes>.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
V=$1; S=$2; N=$3; LANE=(); [[ "${4-}" == --fast ]] && LANE=(--fast)
export FX2_BIN="$REPO/build/exp/C1/bin/cmix"
export E2_LOGIT="${E2_LOGIT-1}" BITL_LR_E6="${BITL_LR_E6-3000}" BITL_ACCUM="${BITL_ACCUM-1}"
P=patches/fx2-cmix-transformer
scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "fx2T+C1-$V" --slice "data/slices/$S" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" \
  --notes "C1 combo: E1 gate3 + E2 logit [E2_LOGIT=$E2_LOGIT] + E3 agg16 [BITL_LR_E6=$BITL_LR_E6 BITL_ACCUM=$BITL_ACCUM] + E9${E9_FXCM_FINAL:+ [E9_FXCM_FINAL=$E9_FXCM_FINAL]}; $N; timing contended"
