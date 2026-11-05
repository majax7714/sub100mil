#!/usr/bin/env bash
# E1 (transformer confidence as a context) build. Same pipeline as patches/fx2-cmix-transformer/build.sh
# (g++ PGO+LTO, x86-64-v3, AVX-512/RCPPS/RSQRTPS/libmvec guards), but only the release cmix, built in
# build/exp/E1/tree = third_party/fx2-cmix-transformer-v1 + record patches + 0001-E1-confidence-contexts.patch.
# Variants are compile-time defines (see the patch / README.md):
#   build.sh gate3 -DE1CONF -DE1_GATE=3      -> build/exp/E1/bin/cmix-gate3
#   build.sh base                            -> unmodified record (no E1 defines; binary == record cmix)
# Run through bench with run.sh <variant> <slice> <notes>.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E="$REPO/build/exp/E1"; SRC="$REPO/third_party/fx2-cmix-transformer-v1"
V="$1"; shift; DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 $*"
if [[ ! -d "$E/tree" ]]; then
  [[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]]
  mkdir -p "$E"
  rsync -a --exclude .git --exclude 'models/*.tch' \
    --exclude 'cpp_infer/bench/alu_peaks' --exclude 'cpp_infer/bench/gemv' --exclude 'cpp_infer/bench/gemv_clang' \
    --exclude 'cpp_infer/bench/l3_thrasher' --exclude 'cpp_infer/bench/membw' --exclude 'cpp_infer/bench/misc' \
    "$SRC/" "$E/tree/"
  ( cd "$E/tree" && git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm pristine
    for p in "$REPO"/patches/fx2-cmix-transformer/*.patch "$REPO"/patches/exp/E1-confidence/*.patch; do git apply "$p"; done )
fi
mkdir -p "$E/bin"
cd "$E/tree"
L="$E/logs/$V"; mkdir -p "$L"
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen > "$L/build_profgen.log" 2>&1
./cmix -c ./prof_input/input ./prof_comp > /dev/null 2>&1
rm -f prof_comp ppm.temp progress.log
make CFLAGS_DEFINES="$DEFS" prof_use > "$L/build_profuse.log" 2>&1
dis=$(objdump -d --insn-width=16 cmix)
for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
  if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
done
if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
cp cmix "$E/bin/cmix-$V"
echo "built $E/bin/cmix-$V"
