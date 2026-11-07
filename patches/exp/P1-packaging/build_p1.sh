#!/usr/bin/env bash
# [P1] C3 + all P1 code-size changes (patches/exp/P1-packaging): E12 without std::regex, FX2TFWC3 weight decoder,
# bit_head at -O2, no unwind tables in the CM objects, rank-delta article order ($E/new_article_order.rd is the
# order file to CM-code into comp9 instead of src/readalike_prepr/data/new_article_order). Otherwise C3-combo/build.sh verbatim. Output: build/exp/P1/c3p1/bin/cmix (P1_OUT overrides c3p1).
# The archive must be bit-identical to C3's; the weights file to embed is made by mk_tfwc3.sh.
# C3 = C2 (C1 + E2 logit LR x0.5 + E12) + X2 metadata time law (final stage after E12), one build on
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
# Build: the record's pipeline (g++ PGO+LTO, -march=x86-64-v3, AVX-512/EVEX and RCPPS/RSQRTPS/libmvec guards),
# release cmix only, compile-time -DE1CONF -DE1_GATE=3. Output: build/exp/C3/bin/cmix.
# Runtime env (BOTH comp and decomp): see run.sh (C1's env + E2_A0/E2_AF + E12_DICT/E12_LR); the PGO training run
# uses the same env so the profiled paths are the ones C2 runs.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"; E="$REPO/build/exp/P1/${P1_OUT:-c3p1}"; P="$REPO/patches"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3"
# [P1] no unwind tables in the CM objects (they are -fno-exceptions): -20 KB of .eh_frame; the instructions are
# identical (checked: normalized objdump of the C3 build with/without equal), only addresses move
DEFS="$DEFS -fno-asynchronous-unwind-tables -fno-unwind-tables"
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
# [P1] E12 without std::regex (same matches, ~29 KB less compressed code)
cp "$P"/exp/P1-packaging/e12_re.h src/models/e12_re.h
git apply "$P"/exp/P1-packaging/0001-P1-e12-no-regex.patch
# [P1] FX2TFWC3 weights (int4 tree 16-bit probs, shift 9): decoder + encoder; decodes to the same tensors
git apply "$P"/exp/P1-packaging/0002-P1-tfwc3-weights.patch
# [P1] article order stored as rank deltas (comp9 only; the archive does not change): reader + its decoder
cp "$P"/exp/P1-packaging/order_rankdelta.h src/readalike_prepr/order_rankdelta.h
git apply "$P"/exp/P1-packaging/0004-P1-order-rankdelta.patch
python3 "$P"/exp/P1-packaging/order_rankdelta.py src/readalike_prepr/data/new_article_order "$E/new_article_order.rd"
# [P1, experimental, P1_OS=1] E12 and X2 namespaces at -Os (#pragma GCC optimize); must be re-verified bit-exact
if [[ "${P1_OS-}" == 1 ]]; then git apply "$P"/exp/P1-packaging/0003-P1-e12-x2-Os.patch; fi
# [P1] E3's bit_head at -O2 instead of -O3 (value-safe FP: no fast-math, -ffp-contract=off, so the results
# cannot depend on the optimization level); ~13 KB less code
sed -i 's/^\t$(CC) -m64 -O3 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/\t$(CC) -m64 -O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/' makefile
grep -q -- '-O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off' makefile
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/build_profgen.log" 2>&1
env E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1 E2_A0=0.25 E2_AF=0.05 E12_DICT=dictionary/english.dic E12_LR=0.004 X2_DICT=dictionary/english.dic \
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
