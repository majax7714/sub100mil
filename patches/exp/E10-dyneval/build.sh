#!/usr/bin/env bash
# E10 build: same pipeline as patches/fx2-cmix-transformer/build.sh (g++ PGO+LTO, -march=x86-64-v3,
# upstream's AVX-512/EVEX and RCPPS/RSQRTPS/libmvec guards), but only the release `cmix`.
# Usage: build.sh [TREE] [OUT_BIN]
#   TREE    default build/exp/E10/tree (record tree with patches/fx2-cmix-transformer/*.patch and
#           patches/exp/E2-correction-head/*.patch and this directory's *.patch applied; see README.md)
#   OUT_BIN default build/exp/E10/cmix
# Variants are selected at runtime by the E10_* (and E2_*) environment variables (README.md), so one build
# serves every variant; comp and decomp must see the same E10_*/E2_* values.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TREE="$(realpath "${1:-$REPO/build/exp/E10/tree}")"
OUT="$(realpath -m "${2:-$REPO/build/exp/E10/cmix}")"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000"
cd "$TREE"
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]
LOG="$OUT.build.log"; : > "$LOG"
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen >> "$LOG" 2>&1
./cmix -c ./prof_input/input ./prof_comp > /dev/null 2>&1
rm -f prof_comp ppm.temp progress.log
make CFLAGS_DEFINES="$DEFS" prof_use >> "$LOG" 2>&1
dis=$(objdump -d --insn-width=16 cmix)
for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
  if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
done
if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
mkdir -p "$(dirname "$OUT")"; mv cmix "$OUT"
echo "built $OUT ($(stat -c %s "$OUT") B)"
