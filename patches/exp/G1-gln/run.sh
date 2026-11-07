#!/usr/bin/env bash
# run.sh <name> <slice-name|path> <notes> [--fast]: slot-limited bench.sh round trip of build/exp/G1/bin/cmix (or $G1_BIN)
# in slice mode with C3's runtime configuration plus G1_GLN=1 (GLN final stage; G1_N/K/LR/LRMIN/BSCALE/SEED/TAU pass
# through to BOTH directions, record them in <name>/<notes>). Method name fx2T+G1-<name>.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
V=$1; S=$2; N=$3; LANE=(); [[ "${4-}" == --fast ]] && LANE=(--fast)
export FX2_BIN="${G1_BIN:-$REPO/build/exp/G1/bin/cmix}"
export E2_LOGIT="${E2_LOGIT-1}" BITL_LR_E6="${BITL_LR_E6-3000}" BITL_ACCUM="${BITL_ACCUM-1}"
export E2_A0="${E2_A0-0.25}" E2_AF="${E2_AF-0.05}"
export E12_DICT="${E12_DICT-$REPO/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic}" E12_LR="${E12_LR-0.004}"
export X2_DICT="${X2_DICT-$E12_DICT}" G1_GLN="${G1_GLN-1}"
G1ENV="G1_GLN=$G1_GLN"; for v in G1_N G1_K G1_LR G1_LRMIN G1_BSCALE G1_SEED G1_TAU; do [[ -n "${!v-}" ]] && { export "$v"; G1ENV+=" $v=${!v}"; }; done
P=patches/fx2-cmix-transformer
scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "fx2T+G1-$V" --slice "$( [[ $S == */* ]] && echo "$S" || echo "data/slices/$S")" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" \
  --notes "G1 GLN final stage after the BITLSTM head [$G1ENV] on C3 (C2 + X2; C2 = C1 + E2 LR x0.5 + E12 [E12_LR=$E12_LR]); bin $(basename "$(dirname "$(dirname "$FX2_BIN")")")/$(basename "$FX2_BIN"); $N; timing contended"
