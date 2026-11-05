#!/usr/bin/env bash
# E9 (feed fxcm's real final prediction to layer 1 and auxiliary_context_) on fx2-cmix-transformer-v1:
# set up build/exp/E9/tree (pinned third_party copy + patches/fx2-cmix-transformer/*.patch + this dir's *.patch)
# and build the release cmix with the record's pipeline (g++ PGO+LTO, -march=x86-64-v3, AVX-512/EVEX and
# RCPPS/RSQRTPS/libmvec guards). Output: build/exp/E9/bin/cmix.
# The fix is on by default; E9_FXCM_FINAL=0 (set for BOTH comp and decomp) restores the record's behaviour.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"; E="$REPO/build/exp/E9"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000"
[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
mkdir -p "$E/bin" "$E/logs"; rm -rf "$E/tree"
rsync -a --exclude .git --exclude 'models/*.tch' --exclude 'cpp_infer/bench' "$SRC/" "$E/tree/"
cd "$E/tree"
git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm pristine
for p in "$REPO"/patches/fx2-cmix-transformer/*.patch "$REPO"/patches/exp/E9-fxcm-final/*.patch; do git apply "$p"; done
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/build_profgen.log" 2>&1
./cmix -c ./prof_input/input ./prof_comp > /dev/null 2>&1
rm -f prof_comp ppm.temp progress.log
make CFLAGS_DEFINES="$DEFS" prof_use > "$E/logs/build_profuse.log" 2>&1
dis=$(objdump -d --insn-width=16 cmix)
for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
  if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
done
if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
cp cmix "$E/bin/cmix"
echo "built $E/bin/cmix ($(stat -c %s "$E/bin/cmix") B)"
