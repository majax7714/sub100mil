#!/usr/bin/env bash
# run.sh <variant> <slice-name> <notes>: slot-limited bench.sh round trip of build/exp/E1/bin/cmix-<variant>
# in slice mode (patches/fx2-cmix-transformer/slice_cmd.sh). Needs the record's build (slicetool, dict, weights).
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."
V=$1; S=$2; N=$3
P=patches/fx2-cmix-transformer; B="$PWD/build/exp/E1/bin/cmix-$V"
scripts/slot.sh scripts/bench.sh --method "fx2T+E1-$V" --slice data/slices/$S \
  --comp "FX2_BIN=$B $P/slice_cmd.sh comp {in} {out}" \
  --decomp "FX2_BIN=$B $P/slice_cmd.sh decomp {in} {out}" \
  --notes "E1 confidence-as-context: $N; timing contended"
