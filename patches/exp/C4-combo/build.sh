#!/usr/bin/env bash
# C4 submission build = C3-full (switches baked) + T1 + G1 (baked) + P1's packaging. Outputs in build/exp/C4/bin/:
#   cmix           the clean submission binary, unpacked (reads no env variable except the diagnostic
#                  FX2_PRINT_VOCAB; E12/X2/T1/G1 on iff a dictionary is in use)
#   cmix_packed    cmix packed with P1's p1cm stub (patches/exp/P1-packaging/build_stub.sh)
#   comp9          cmix_packed + comp_dict + comp_order (rank-delta order) + comp_tfweights (FX2TFWC3) + header,
#                  as patches/exp/P1-packaging/build_c3full_p1.sh; comp9.parts lists the part sizes
#   cmix_costlog   the same tree + 0002-costlog-on-C4.patch (FX2_COSTLOG=<path>: the 40 B/byte cost log; coded p
#                  after G1), unpacked
#   comp9_costlog  cmix_costlog + the SAME comp_dict/comp_order/tfweights/header as comp9 (the full run's compressor;
#                  full_cmd.sh swaps cmix_packed in as archive9's decompressor)
#   6m-q4-fp32.tfwc3, new_article_order.rd: P1's weight and order files (the slice benches use the .tfwc3)
# Pipeline: the record's (g++ PGO+LTO, -march=x86-64-v3, AVX-512/EVEX, RCPPS/RSQRTPS and libmvec guards) with
# P1's code-size flags (no unwind tables in the CM objects, bit_head at -O2); defines -DSEED=923 -DUPDATE_LIMIT=3000
# -DE1CONF -DE1_GATE=3; PGO trained with the dictionary (./cmix -c dictionary/english.dic prof_input/input under
# env -i), so E12/X2/T1/G1 are profiled. Fails if any getenv other than FX2_PRINT_VOCAB remains.
# OUT (default build/exp/C4) = output directory.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E="$(realpath -m "${OUT:-$REPO/build/exp/C4}")"; H="$REPO/patches/exp/C4-combo"; P="$REPO/patches/exp/P1-packaging"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3 -fno-asynchronous-unwind-tables -fno-unwind-tables"
mkdir -p "$E/bin" "$E/logs"

mk() {  # mk TREE [extra patch]: C4 tree + P1's bit_head -O2
  "$H/mktree.sh" "$1"
  ( cd "$1"
    [[ -z "${2-}" ]] || git apply "$2"
    sed -i 's/^\t$(CC) -m64 -O3 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/\t$(CC) -m64 -O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/' makefile
    grep -q -- '-O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off' makefile )
}

pgo_build() {  # pgo_build TREE OUTBIN LOGTAG
  local T=$1 OUTB=$2 tag=$3
  ( cd "$T"
    rm -rf pgo_data && mkdir -p pgo_data
    make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/${tag}_profgen.log" 2>&1
    env -i PATH="$PATH" HOME="$HOME" ./cmix -c dictionary/english.dic ./prof_input/input ./prof_comp > "$E/logs/${tag}_train.log" 2>&1
    grep -q '^T1: interlanguage field copy on' "$E/logs/${tag}_train.log" && grep -q '^G1: GLN final stage on' "$E/logs/${tag}_train.log" \
      || { echo "FAIL: $tag PGO training did not run T1/G1" >&2; exit 1; }
    rm -f prof_comp ppm.temp progress.log
    make CFLAGS_DEFINES="$DEFS" prof_use > "$E/logs/${tag}_profuse.log" 2>&1
    dis=$(objdump -d --insn-width=16 cmix)
    for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
      if grep -qE "$pat" <<< "$dis"; then echo "FAIL: $tag cmix matches '$pat'" >&2; exit 1; fi
    done
    if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: $tag cmix uses glibc libmvec" >&2; exit 1; fi
    cp cmix "$OUTB" )
  echo "built $OUTB ($(stat -c %s "$OUTB") B)"
}

# 1. clean submission binary
mk "$E/tree"
left=$(grep -rn 'getenv' "$E/tree/src" --include='*.cpp' --include='*.h' | grep -v FX2_PRINT_VOCAB || true)
[[ -z "$left" ]] || { echo "FAIL: getenv left in the clean tree:" >&2; echo "$left" >&2; exit 1; }
pgo_build "$E/tree" "$E/bin/cmix" clean

# 2. costlog twin (same tree + 0002)
mk "$E/tree_costlog" "$H/0002-costlog-on-C4.patch"
pgo_build "$E/tree_costlog" "$E/bin/cmix_costlog" costlog

# 3. P1 streams, packed exe, comp9 (as patches/exp/P1-packaging/build_c3full_p1.sh)
python3 "$P/order_rankdelta.py" "$E/tree/src/readalike_prepr/data/new_article_order" "$E/bin/new_article_order.rd"
python3 "$P/mk_tfwc3.py" "$E/tree/models/6m-q4-fp32.tfwc2" "$E/bin/6m-q4-fp32.tfwc3" "$E/tree"
"$P/build_stub.sh" "$E/bin/cmix" "$E/bin/cmix_packed"
R="$E/run"; rm -rf "$R"; mkdir -p "$R"; cp "$E/bin/cmix_packed" "$R/cmix_orig"
( cd "$R"
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/tree/dictionary/english.dic" ./comp_dict > /dev/null 2>&1
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/bin/new_article_order.rd" ./comp_order > /dev/null 2>&1
  cp "$E/bin/6m-q4-fp32.tfwc3" ./comp_tfweights
  ./cmix_orig -h "$(wc -c < comp_dict)" "$(wc -c < comp_order)" 0 "$(wc -c < comp_tfweights)"
  cat cmix_orig comp_dict comp_order comp_tfweights header.dat > comp9 && chmod +x comp9
  cp "$E/bin/cmix_costlog" cmix_costlog
  cat cmix_costlog comp_dict comp_order comp_tfweights header.dat > comp9_costlog && chmod +x comp9_costlog
  rm -f ppm.temp progress.log *.cmix.temp
  wc -c cmix_orig comp_dict comp_order comp_tfweights header.dat comp9 cmix_costlog comp9_costlog | tee "$E/bin/comp9.parts" )
cp "$R/comp9" "$R/comp9_costlog" "$E/bin/"
echo "built $E/bin/{cmix,cmix_packed,comp9,cmix_costlog,comp9_costlog}"
