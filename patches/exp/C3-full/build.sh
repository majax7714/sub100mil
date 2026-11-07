#!/usr/bin/env bash
# C3 submission build (full mode) + its per-byte-cost twin. Outputs in build/exp/C3-full/bin/:
#   cmix          C3 with every runtime switch baked in (0001-C3-bake-defaults.patch): reads no env variable that
#                 changes coding, E12/X2 read the dictionary the stream is coded with (./.dict in -e / archive9 mode)
#   comp9         the self-extracting compressor: cmix + comp_dict + comp_order + comp_tfweights + header
#                 (as patches/fx2-cmix-transformer/build.sh, no UPX); comp9.parts lists the part sizes
#   cmix_costlog  the same + 0002-costlog-on-C3.patch (FX2_COSTLOG=<path> writes the 40 B/byte log; observation only)
#   comp9_costlog cmix_costlog + the SAME comp_dict/comp_order/tfweights/header as comp9
# Both binaries: the record's pipeline (g++ PGO+LTO, -march=x86-64-v3, AVX-512/EVEX, RCPPS/RSQRTPS and libmvec
# guards), defines -DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3.
# PGO training (PGO_TRAIN): dict (default) = ./cmix -c dictionary/english.dic prof_input/input, which runs E12/X2 (they
# are on only with a dictionary); nodict = upstream's ./cmix -c prof_input/input, which leaves E12/X2 unprofiled and
# gives a 74 KB larger binary (686,456 vs 612,760 B; partial-training compiles never-run code at -O3, not -Os).
# OUT (default build/exp/C3-full) = output directory.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E="$(realpath -m "${OUT:-$REPO/build/exp/C3-full}")"; H="$REPO/patches/exp/C3-full"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3"
mkdir -p "$E/bin" "$E/logs"

pgo_build() {  # pgo_build TREE OUTBIN LOGTAG
  local T=$1 OUTB=$2 tag=$3
  ( cd "$T"
    rm -rf pgo_data && mkdir -p pgo_data
    make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/${tag}_profgen.log" 2>&1
    if [[ "${PGO_TRAIN:-dict}" == dict ]]; then
      env -i PATH="$PATH" HOME="$HOME" ./cmix -c dictionary/english.dic ./prof_input/input ./prof_comp > /dev/null 2>&1
    else
      env -i PATH="$PATH" HOME="$HOME" ./cmix -c ./prof_input/input ./prof_comp > /dev/null 2>&1
    fi
    rm -f prof_comp ppm.temp progress.log
    make CFLAGS_DEFINES="$DEFS" prof_use > "$E/logs/${tag}_profuse.log" 2>&1
    dis=$(objdump -d --insn-width=16 cmix)
    for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
      if grep -qE "$pat" <<< "$dis"; then echo "FAIL: $tag cmix matches '$pat'" >&2; exit 1; fi
    done
    if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: $tag cmix uses glibc libmvec" >&2; exit 1; fi
    # no getenv left except the record's diagnostic FX2_PRINT_VOCAB (and FX2_COSTLOG in the costlog twin)
    cp cmix "$OUTB" )
  echo "built $OUTB ($(stat -c %s "$OUTB") B)"
}

# 1. clean submission binary
"$H/mktree.sh" "$E/tree"
left=$(grep -rn 'getenv' "$E/tree/src" --include='*.cpp' --include='*.h' | grep -v FX2_PRINT_VOCAB || true)
[[ -z "$left" ]] || { echo "FAIL: getenv left in the clean tree:" >&2; echo "$left" >&2; exit 1; }
pgo_build "$E/tree" "$E/bin/cmix" clean

# 2. costlog twin (same tree + 0002)
"$H/mktree.sh" "$E/tree_costlog"
( cd "$E/tree_costlog" && git apply "$H/0002-costlog-on-C3.patch" )
pgo_build "$E/tree_costlog" "$E/bin/cmix_costlog" costlog

# 3. comp9 (as patches/fx2-cmix-transformer/build.sh: the binary CM-compresses its own dictionary and article order)
R="$E/run"; rm -rf "$R"; mkdir -p "$R"; cp "$E/bin/cmix" "$R/cmix_orig"
( cd "$R"
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/tree/dictionary/english.dic" ./comp_dict > /dev/null 2>&1
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/tree/src/readalike_prepr/data/new_article_order" ./comp_order > /dev/null 2>&1
  cp "$E/tree/models/6m-q4-fp32.tfwc2" ./comp_tfweights
  ./cmix_orig -h "$(wc -c < comp_dict)" "$(wc -c < comp_order)" 0 "$(wc -c < comp_tfweights)"
  cat cmix_orig comp_dict comp_order comp_tfweights header.dat > comp9 && chmod +x comp9
  cp "$E/bin/cmix_costlog" cmix_costlog
  cat cmix_costlog comp_dict comp_order comp_tfweights header.dat > comp9_costlog && chmod +x comp9_costlog
  rm -f ppm.temp progress.log *.cmix.temp
  wc -c cmix_orig comp_dict comp_order comp_tfweights header.dat comp9 cmix_costlog comp9_costlog | tee "$E/bin/comp9.parts" )
cp "$R/comp9" "$R/comp9_costlog" "$E/bin/"
echo "built $E/bin/{cmix,comp9,cmix_costlog,comp9_costlog}"
