#!/usr/bin/env bash
# G1 = C3 + GLN final stage. C3 = C2 (C1 + E2 logit LR x0.5 + E12) + X2 metadata time law (final stage after E12), one build on
# fx2-cmix-transformer-v1. Steps 1-8 are C2's (patches/exp/C2-combo/build.sh); 9-10 add X2. E2's LR x0.5 is a runtime setting (run.sh). Tree build/exp/C2/tree = pinned third_party
# copy + patches, applied in this order:
#   1. patches/fx2-cmix-transformer/*.patch                     (the record's gcc port, slice vocab, slicetool)
#   2. patches/exp/E1-confidence/*.patch                        (clean)
#   3. patches/exp/E9-fxcm-final/*.patch                        (clean)
#   4. patches/exp/E2-correction-head/*.patch minus src/predictor.*   (transformer side: model_opt, arena_build)
#   5. patches/exp/E3-bitlstm/*.patch         minus src/predictor.*   (bit_head, coder, makefile, model_opt accessor)
#   6. patches/exp/C1-combo/0001-C1-predictor-merge-E2-E3.patch (E2's and E3's predictor.{h,cpp} hunks merged
#      on top of E1+E9; the only new decision is documented in README.md)
#   7. patches/exp/E12-arith/0001-e12-arith-predictor.patch minus src/predictor.cpp (PredictorOptions, member,
#      include, runner hook) + patches/exp/E12-arith/arith.h as src/models/e12_arith.h
#   8. patches/exp/C2-combo/0001-C2-predictor-E12.patch (E12's predictor.cpp hunks on top of C1: the final stage
#      goes after SSE and E1's APM, before the transformer override, as in E12 alone)
#   9. patches/exp/X2-timelaw/0001-x2-timelaw-predictor.patch minus src/predictor.cpp (include, member) +
#      patches/exp/X2-timelaw/x2_timelaw.h as src/models/x2_timelaw.h
#  10. patches/exp/C3-combo/0001-C3-predictor-X2.patch (X2's three predictor.cpp lines next to E12's in C2's file)
#  11. patches/exp/G1-gln/g1_gln.h as src/models/g1_gln.h + 0001-G1-gln-final-stage.patch (GLN stage after the BITLSTM head)
# Build: the record's pipeline (g++ PGO+LTO, -march=x86-64-v3, AVX-512/EVEX and RCPPS/RSQRTPS/libmvec guards),
# release cmix only, compile-time -DE1CONF -DE1_GATE=3. Output: build/exp/G1/bin/cmix (tree build/exp/G1/tree).
# Runtime env (BOTH comp and decomp): see run.sh (C1's env + E2_A0/E2_AF + E12_DICT/E12_LR); the PGO training run
# uses the same env so the profiled paths are the ones C2 runs.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"; E="$REPO/build/exp/G1"; P="$REPO/patches"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3"
[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
mkdir -p "$E/bin" "$E/logs"; rm -rf "$E/tree"
rsync -a --exclude .git --exclude 'models/*.tch' --exclude 'cpp_infer/bench' "$SRC/" "$E/tree/"
cd "$E/tree"
git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm pristine
for p in "$P"/fx2-cmix-transformer/*.patch "$P"/exp/E1-confidence/*.patch "$P"/exp/E9-fxcm-final/*.patch; do git apply "$p"; done
for p in "$P"/exp/E2-correction-head/*.patch "$P"/exp/E3-bitlstm/*.patch; do git apply --exclude='src/predictor.*' "$p"; done
git apply "$P"/exp/C1-combo/0001-C1-predictor-merge-E2-E3.patch
git apply --exclude='src/predictor.cpp' "$P"/exp/E12-arith/0001-e12-arith-predictor.patch
cp "$P"/exp/E12-arith/arith.h src/models/e12_arith.h
git apply "$P"/exp/C2-combo/0001-C2-predictor-E12.patch
git apply --exclude='src/predictor.cpp' "$P"/exp/X2-timelaw/0001-x2-timelaw-predictor.patch
cp "$P"/exp/X2-timelaw/x2_timelaw.h src/models/x2_timelaw.h
git apply "$P"/exp/C3-combo/0001-C3-predictor-X2.patch
cp "$P"/exp/G1-gln/g1_gln.h src/models/g1_gln.h
git apply "$P"/exp/G1-gln/0001-G1-gln-final-stage.patch
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/build_profgen.log" 2>&1
env E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1 E2_A0=0.25 E2_AF=0.05 E12_DICT=dictionary/english.dic E12_LR=0.004 X2_DICT=dictionary/english.dic G1_GLN=1 \
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
