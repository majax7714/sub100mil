#!/usr/bin/env bash
# Build fx2-cmix-transformer-v1 (third_party, pinned 83f2603) with g++ on Fedora, AVX2 only.
# Copies the pristine third_party tree to build/refs/fx2-cmix-transformer-v1/tree, applies the
# patches in this directory, and produces in build/refs/fx2-cmix-transformer-v1/:
#   cmix         release binary (g++ PGO + LTO, -march=x86-64-v3), same flags as upstream modulo gcc
#   comp9        self-extracting enwik9 compressor (= upstream run/cmix, but NOT upx-packed)
#   cmix_noPGO   same sources/flags without PGO/LTO (baseline for the profiling build)
#   cmix_prof    -DFX2_PROF instrumented build (per-model rdtsc breakdown), no PGO
#   cmix_syms    release binary with symbols (perf record)
#   slicetool    prefix-of-enwik9 version of the -e preprocessing (see 0002 patch)
# No network, no sudo, writes only under build/. Upstream's build_and_construct_comp.sh needs
# clang++-17, llvm-profdata-17 and upx-ucl, none of which are installed here.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"
OUT="$REPO/build/refs/fx2-cmix-transformer-v1"
PATCHES="$REPO/patches/fx2-cmix-transformer"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000"   # as in build_and_construct_comp.sh

[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
mkdir -p "$OUT"
rm -rf "$OUT/tree"
rsync -a --exclude .git --exclude 'models/*.tch' \
  --exclude 'cpp_infer/bench/alu_peaks' --exclude 'cpp_infer/bench/gemv' --exclude 'cpp_infer/bench/gemv_clang' \
  --exclude 'cpp_infer/bench/l3_thrasher' --exclude 'cpp_infer/bench/membw' --exclude 'cpp_infer/bench/misc' \
  "$SRC/" "$OUT/tree/"
cd "$OUT/tree"
# patch(1) is not installed; git apply needs its own repo here (inside sub100mil it would
# silently skip these gitignored paths)
git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm "pristine 83f2603"
for p in "$PATCHES"/*.patch; do git apply --check "$p" && git apply "$p"; done
git diff --stat | tail -1

# upstream's dictionary guard
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]

# NB: upstream's "prof_gen: clean cmix" races under make -j (clean deletes tf_*.o); build serially.
make CFLAGS_DEFINES="$DEFS" cmix > "$OUT/build_noPGO.log" 2>&1 && mv cmix "$OUT/cmix_noPGO"
make CFLAGS_DEFINES="$DEFS" prof > "$OUT/build_prof.log" 2>&1 && mv cmix "$OUT/cmix_prof"
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen > "$OUT/build_profgen.log" 2>&1
./cmix -c ./prof_input/input ./prof_comp > /dev/null 2>&1   # upstream's PGO training run
rm -f prof_comp ppm.temp progress.log
make CFLAGS_DEFINES="$DEFS" prof_use > "$OUT/build_profuse.log" 2>&1
make slicetool > /dev/null 2>&1 && mv slicetool "$OUT/slicetool"

# upstream's portability guards (AVX2 baseline, no vendor-dependent reciprocal estimates)
dis=$(objdump -d --insn-width=16 cmix)
for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
  if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
done
if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
cp cmix "$OUT/cmix"
# same PGO profile, symbols kept (for perf record); .text must match the release binary
make CFLAGS_DEFINES="$DEFS" STRIP= prof_use > "$OUT/build_syms.log" 2>&1 && mv cmix "$OUT/cmix_syms"
objcopy -O binary -j .text "$OUT/cmix" "$OUT/.text.rel"; objcopy -O binary -j .text "$OUT/cmix_syms" "$OUT/.text.sym"
cmp -s "$OUT/.text.rel" "$OUT/.text.sym" && echo ".text of cmix_syms == cmix" || echo "WARNING: cmix_syms .text differs from cmix"
rm -f "$OUT/.text.rel" "$OUT/.text.sym"; cp "$OUT/cmix" cmix

# self-extracting compressor, as in build_and_construct_comp.sh (no upx-ucl)
rm -rf run && mkdir -p run && cp ./cmix run/cmix_orig
( cd run
  ./cmix_orig -c ../dictionary/english.dic ./comp_dict > /dev/null 2>&1
  ./cmix_orig -c ../src/readalike_prepr/data/new_article_order ./comp_order > /dev/null 2>&1
  cp ../models/6m-q4-fp32.tfwc2 ./comp_tfweights
  ./cmix_orig -h "$(wc -c < comp_dict)" "$(wc -c < comp_order)" 0 "$(wc -c < comp_tfweights)"
  cat cmix_orig comp_dict comp_order comp_tfweights header.dat > cmix && chmod +x cmix
  rm -f ppm.temp progress.log *.cmix.temp
  wc -c cmix_orig comp_dict comp_order comp_tfweights header.dat cmix )
cp run/cmix "$OUT/comp9"
g++ -O2 -march=x86-64-v3 "$PATCHES/tools/membw.cpp" -o "$OUT/membw"
echo "built: $OUT/{cmix,comp9,cmix_noPGO,cmix_prof,slicetool}"
