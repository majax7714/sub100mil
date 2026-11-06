#!/usr/bin/env bash
# E12 build: same pipeline as patches/fx2-cmix-transformer/build.sh (g++ PGO+LTO, -march=x86-64-v3,
# upstream's AVX-512/EVEX and RCPPS/RSQRTPS/libmvec guards), but only the release `cmix`.
# Usage: build.sh [TREE] [OUT_BIN]
#   TREE    default build/exp/E12/tree (recreated from scratch: pinned third_party, the record's patches,
#           this directory's *.patch and arith.h)
#   OUT_BIN default build/exp/E12/cmix
# The specialist is switched on at runtime by E12_DICT=<english.dic path>; comp and decomp must
# both set it (patches/exp/E12-arith/README.md).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TREE="$(realpath -m "${1:-$REPO/build/exp/E12/tree}")"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"
[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
# fresh tree: pinned third_party + the record's patches + arith.h (as src/models/e12_arith.h) + this directory's patch
rm -rf "$TREE"
rsync -a --exclude .git --exclude 'models/*.tch' --exclude 'cpp_infer/bench' "$SRC/" "$TREE/"
(cd "$TREE" && git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm pristine
 for p in "$REPO"/patches/fx2-cmix-transformer/*.patch "$REPO"/patches/exp/E12-arith/*.patch; do git apply "$p"; done)
cp "$REPO/patches/exp/E12-arith/arith.h" "$TREE/src/models/e12_arith.h"
OUT="$(realpath -m "${2:-$REPO/build/exp/E12/cmix}")"
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
