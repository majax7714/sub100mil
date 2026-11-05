#!/usr/bin/env bash
# Build the record (fx2-cmix-transformer-v1) with the per-byte cost log hook.
#   patches/tools/per-byte-cost/build.sh [OUTDIR]      (default build/exp/costlog)
# Copies the pinned third_party tree to OUTDIR/tree, applies patches/fx2-cmix-transformer/*.patch
# and then this directory's *.patch, and builds OUTDIR/cmix_costlog (same flags as the record's
# cmix_noPGO: g++, -march=x86-64-v3, -DSEED=923 -DUPDATE_LIMIT=3000; no PGO, which does not change
# the output). Runs the record's portability guards. Never touches third_party/.
# Other experiments can stack their own patches: put them in OUTDIR/tree after this script and
# re-run `make CFLAGS_DEFINES="-DSEED=923 -DUPDATE_LIMIT=3000" cmix` there.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"
OUT="$(realpath -m "${1:-$REPO/build/exp/costlog}")"
HERE="$REPO/patches/tools/per-byte-cost"
[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
mkdir -p "$OUT"
rm -rf "$OUT/tree"
rsync -a --exclude .git --exclude 'models/*.tch' --exclude 'cpp_infer/bench/*' "$SRC/" "$OUT/tree/"
cd "$OUT/tree"
git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm "pristine 83f2603"
for p in "$REPO"/patches/fx2-cmix-transformer/*.patch "$HERE"/*.patch; do git apply --check "$p" && git apply "$p"; done
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]
make CFLAGS_DEFINES="-DSEED=923 -DUPDATE_LIMIT=3000" cmix > "$OUT/build.log" 2>&1
dis=$(objdump -d --insn-width=16 cmix)
for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
  if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
done
if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
cp cmix "$OUT/cmix_costlog"
echo "built: $OUT/cmix_costlog"
