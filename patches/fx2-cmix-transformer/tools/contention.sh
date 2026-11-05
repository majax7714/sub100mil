#!/usr/bin/env bash
# Transformer-in-situ contention test. Runs `cmix -c ... --transformer W --transformer-only`
# (PPMD + transformer only, every other model skipped) on a slice pinned to CPU $CORE, alone and
# with a membw streamer of various working-set sizes pinned to another physical core ($CO).
# Also a --ppmd-only run to subtract the PPMD share. Prints wall seconds per configuration.
# Usage: contention.sh <slice> <outdir>   (wrap in flock: it is a timing run)
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
R="$REPO/build/refs/fx2-cmix-transformer-v1"
CORE=${CORE:-2}; CO=${CO:-4}
slice="$(realpath "$1")"; out="$(realpath -m "$2")"; mkdir -p "$out"; cd "$out"
"$R/slicetool" pre "$slice" s.pre 2>/dev/null
run() {  # label, extra cmix flags
  local label="$1"; shift
  /usr/bin/time -f "%e" -o t.txt taskset -c "$CORE" "$R/cmix" -c "$R/tree/dictionary/english.dic" s.pre o.bin "$@" > log.txt 2>&1
  printf '%-34s %8.2f s  %s\n' "$label" "$(cat t.txt)" "$(grep -o 'transformer loss: [0-9.]* nats/token over [0-9]* tokens' log.txt | tail -1)"
  rm -f ppm.temp progress.log o.bin
}
W="$R/tree/models/6m-q4-fp32.tfwc2"
run "ppmd-only (alone)" --ppmd-only
run "transformer-only (alone)" --transformer "$W" --transformer-only
for mib in 0.5 4 8 12 64; do
  taskset -c "$CO" "$R/membw" stream "$mib" 100000 2> co.txt & cp=$!
  sleep 1
  run "transformer-only + ${mib}MiB streamer" --transformer "$W" --transformer-only
  kill "$cp"; wait "$cp" 2>/dev/null || true
done
run "transformer-only (alone, repeat)" --transformer "$W" --transformer-only
rm -f s.pre log.txt t.txt co.txt
