#!/usr/bin/env bash
# X3c: slot-limited bench.sh round trip of the C4 costlog twin (archive identical to the C4 submission binary) on an
# X3c sample, keeping the per-byte cost log.   x3c_bench.sh <sample.xml> <notes> [--fast]
# Cost log: build/exp/X3c/cost/<sample basename>.bin ; bench output: build/exp/X3c/bench/<basename>.txt
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; cd "$REPO"
B="$REPO/build/exp/C4/bin"; S="$(realpath "$1")"; N="$2"; shift 2
LANE=(); for a in "$@"; do [[ $a == --fast ]] && LANE=(--fast); done
X="$REPO/build/exp/X3c"; mkdir -p "$X/cost" "$X/bench" "$X/benchtmp"
b="$(basename "$S" .xml)"; C=$(git rev-parse --short HEAD); P=patches/fx2-cmix-transformer
env -i PATH=/usr/bin:/bin HOME="$HOME" BENCH_TMP="$X/benchtmp" FX2_BIN="$B/cmix_costlog" FX2_WEIGHTS="$B/6m-q4-fp32.tfwc3" \
  scripts/slot.sh "${LANE[@]}" scripts/bench.sh --method "fx2T+C4-full-costlog (X3c order sample)" --slice "$S" \
  --comp "FX2_COSTLOG=$X/cost/$b.bin $P/slice_cmd.sh comp {in} {out}" --decomp "$P/slice_cmd.sh decomp {in} {out}" \
  --decomp-size "$(stat -c %s "$B/comp9")" \
  --notes "X3c arrangement: C4 costlog twin (build/exp/C4/bin/cmix_costlog @ $C, FX2TFWC3 weights), slice mode, sample from scripts/analysis/x3c_mksample.py: $N; timing contended" \
  > "$X/bench/$b.txt" 2>&1
tail -15 "$X/bench/$b.txt"
