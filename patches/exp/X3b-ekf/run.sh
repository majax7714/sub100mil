#!/usr/bin/env bash
# run.sh <slice-name> <notes> [--fast]: slot-limited bench.sh round trip of the X3b build (packed exe) in slice mode
# under a clean environment, as patches/exp/C4-combo/run.sh. --decomp-size = C4's comp9 with X3b's packed exe swapped in.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
B="$REPO/build/exp/X3b/bin"; C4="$REPO/build/exp/C4/bin"; S=$1; N=$2; shift 2
LANE=(); for a in "$@"; do [[ $a == --fast ]] && LANE=(--fast); done
S9=$(( $(stat -c %s "$C4/comp9") - $(stat -c %s "$C4/cmix_packed") + $(stat -c %s "$B/cmix_packed") )); C=$(git rev-parse --short HEAD)
mkdir -p "$REPO/build/exp/X3b/benchtmp"
P=patches/fx2-cmix-transformer
env -i PATH=/usr/bin:/bin HOME="$HOME" BENCH_TMP="$REPO/build/exp/X3b/benchtmp" FX2_BIN="$B/cmix_packed" FX2_WEIGHTS="$C4/6m-q4-fp32.tfwc3" \
  scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "fx2T+C4+X3b-ekf" --slice "data/slices/$S" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" --decomp-size "$S9" \
  --notes "X3b EKF combiner after G1 (K 33, p0 1e-3, q 1e-9) on the C4 submission build, patches/exp/X3b-ekf @ $C, packed exe ($(stat -c %s "$B/cmix_packed") B; C4's $(stat -c %s "$C4/cmix_packed")), env -i; $N; timing contended"
