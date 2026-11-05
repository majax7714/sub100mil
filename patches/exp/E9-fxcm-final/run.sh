#!/usr/bin/env bash
# run.sh <slice-name> <notes> [--fast]: slot-limited bench.sh round trip of build/exp/E9/bin/cmix in slice mode.
# E9_FXCM_FINAL passes through to both directions (default: fix on).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
S=$1; N=$2; LANE=(); [[ "${3-}" == --fast ]] && LANE=(--fast)
export FX2_BIN="$REPO/build/exp/E9/bin/cmix"
P=patches/fx2-cmix-transformer
scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "fx2T+E9${E9_FXCM_FINAL:+-f$E9_FXCM_FINAL}" --slice "data/slices/$S" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" \
  --notes "E9 fxcm real final (output 428, not constant slot 430) into layer 1 + auxiliary_context_${E9_FXCM_FINAL:+ [E9_FXCM_FINAL=$E9_FXCM_FINAL]}; $N; timing contended"
