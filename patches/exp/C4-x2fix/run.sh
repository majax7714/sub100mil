#!/usr/bin/env bash
# run.sh packed|unpacked <slice-name|path> <notes> [--fast] [--keep]: slot-limited bench.sh round trip of the C4
# submission binary in slice mode under a clean environment (env -i: only PATH, HOME, BENCH_TMP and slice_cmd.sh's
# FX2_BIN/FX2_WEIGHTS; the binary reads no env). Weights = P1's FX2TFWC3 file. --decomp-size = the packed comp9.
# Method fx2T+C4-x2fix (packed exe, the submission) or fx2T+C4-x2fix-unpacked.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
B="$REPO/build/exp/x2fix/bin"; V=$1; S=$2; N=$3; shift 3
LANE=(); KEEP=(); for a in "$@"; do [[ $a == --fast ]] && LANE=(--fast); [[ $a == --keep ]] && KEEP=(--keep); done
case "$V" in packed) BIN="$B/cmix_packed"; M="fx2T+C4-x2fix";; unpacked) BIN="$B/cmix"; M="fx2T+C4-x2fix-unpacked";; *) exit 2;; esac
S9=$(stat -c %s "$B/comp9"); C=$(git rev-parse --short HEAD)
mkdir -p "$REPO/build/exp/x2fix/benchtmp"
P=patches/fx2-cmix-transformer
env -i PATH=/usr/bin:/bin HOME="$HOME" BENCH_TMP="$REPO/build/exp/x2fix/benchtmp" FX2_BIN="$BIN" FX2_WEIGHTS="$B/6m-q4-fp32.tfwc3" \
  scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "$M" --slice "$( [[ $S == */* ]] && echo "$S" || echo "data/slices/$S")" \
  --comp "$P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" --decomp-size "$S9" "${KEEP[@]}" \
  --notes "C4 + x2fix (patches/exp/C4-x2fix @ $C: X2 page id tracked across restrictions> lines, bounded used_ index), $V exe ($(stat -c %s "$BIN") B), env -i: C3-full + T1 (lr 0.004) + G1 (n8 k8, z3 = BITLSTM delta) + P1 (p1cm stub, regex-free E12, bit_head -O2, no unwind, FX2TFWC3 weights, rank-delta order); comp9 = $(tr -s ' ' < "$B/comp9.parts" | awk '$2!="total"&&$2!="comp9"&&$2!~/costlog/{printf "%s%s %s", sep, $1, $2; sep=" + "}'); $N; timing contended"
