#!/usr/bin/env bash
# X3a build = C4 (patches/exp/C4-combo) with the article order shipped as rule + residual (order_rule.h) instead of
# P1's rank-delta list. Only the compressor's order block and reorder() change: the coded stream, the archive and
# the slice mode are C4's. Outputs in OUT (default build/exp/X3a/c4x)/bin/:
#   cmix, cmix_packed   the submission binary, unpacked and packed with P1's stub (same pipeline as C4's build.sh)
#   order_rule_enc      the build-time encoder; new_article_order.res = the residual for the fx2 order file
#   comp_order          the residual CM-coded as the construct step codes the order block; comp9.parts
# MAIN = the compressor's .main (enwik9 minus intro and coda; e.g. build/exp/E12/full/.main from a -e smoke run).
# Needs about 6 GB for the PGO training run and the CM coding: run it through scripts/slot.sh.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E="$(realpath -m "${OUT:-$REPO/build/exp/X3a/c4x}")"; H="$REPO/patches/exp/X3a-order-rule"; P="$REPO/patches/exp/P1-packaging"
MAIN="${MAIN:-$REPO/build/exp/E12/full/.main}"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3 -fno-asynchronous-unwind-tables -fno-unwind-tables"
mkdir -p "$E/bin" "$E/logs"
T="$E/tree"; rm -rf "$T"
"$REPO/patches/exp/C4-combo/mktree.sh" "$T" > "$E/logs/mktree.log" 2>&1
( cd "$T"
  cp "$H/order_rule.h" src/readalike_prepr/order_rule.h
  git apply "$H/0001-X3a-order-rule.patch"
  rm -f src/readalike_prepr/order_rankdelta.h
  sed -i 's/^\t$(CC) -m64 -O3 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/\t$(CC) -m64 -O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/' makefile
  grep -q -- '-O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off' makefile
  rm -rf pgo_data && mkdir -p pgo_data
  make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/profgen.log" 2>&1
  env -i PATH="$PATH" HOME="$HOME" ./cmix -c dictionary/english.dic ./prof_input/input ./prof_comp > "$E/logs/train.log" 2>&1
  grep -q '^T1: interlanguage field copy on' "$E/logs/train.log" && grep -q '^G1: GLN final stage on' "$E/logs/train.log" \
    || { echo "FAIL: PGO training did not run T1/G1" >&2; exit 1; }
  rm -f prof_comp ppm.temp progress.log
  make CFLAGS_DEFINES="$DEFS" prof_use > "$E/logs/profuse.log" 2>&1
  dis=$(objdump -d --insn-width=16 cmix)
  for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
    if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
  done
  if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
  cp cmix "$E/bin/cmix" )
"$P/build_stub.sh" "$E/bin/cmix" "$E/bin/cmix_packed"
g++ -O2 -march=x86-64-v3 -o "$E/bin/order_rule_enc" "$H/order_rule_enc.cpp"
"$E/bin/order_rule_enc" enc "$MAIN" "$T/src/readalike_prepr/data/new_article_order" "$E/bin/new_article_order.res" 2> "$E/logs/enc.log"
grep -q 'round trip OK' "$E/logs/enc.log"
R="$E/run"; rm -rf "$R"; mkdir -p "$R"; cp "$E/bin/cmix_packed" "$R/cmix_orig"
( cd "$R"
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/bin/new_article_order.res" ./comp_order > /dev/null 2>&1
  rm -f ppm.temp progress.log *.cmix.temp
  wc -c cmix_orig comp_order | tee "$E/bin/comp9.parts" )
cp "$R/comp_order" "$E/bin/"
echo "built $E/bin/{cmix,cmix_packed,new_article_order.res,comp_order}; raw exe $(stat -c %s "$E/bin/cmix") B"
