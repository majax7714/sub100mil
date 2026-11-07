#!/usr/bin/env bash
# build_c3full_p1.sh: the C3 submission build (patches/exp/C3-full: switches baked, PGO trained with the dictionary)
# + every P1 size change: 0001 E12 without std::regex, 0002 FX2TFWC3 weight decoder, 0004 rank-delta article order,
# bit_head at -O2, no unwind tables in the CM objects. Then the packed exe (p1cm stub) and comp9 with the P1 streams.
# Mirrors patches/exp/C3-full/build.sh step 1 + 3 (no costlog twin). Output: build/exp/P1/c3full_p1/bin/{cmix,
# cmix_packed,comp9,comp9.parts,6m-q4-fp32.tfwc3}. OUT overrides the directory.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E="$(realpath -m "${OUT:-$REPO/build/exp/P1/c3full_p1}")"; H="$REPO/patches/exp/C3-full"; P="$REPO/patches/exp/P1-packaging"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3 -fno-asynchronous-unwind-tables -fno-unwind-tables"
mkdir -p "$E/bin" "$E/logs"
"$H/mktree.sh" "$E/tree"
cd "$E/tree"
cp "$P/e12_re.h" src/models/e12_re.h
git apply "$P/0001-P1-e12-no-regex.patch"
git apply "$P/0002-P1-tfwc3-weights.patch"
cp "$P/order_rankdelta.h" src/readalike_prepr/order_rankdelta.h
git apply "$P/0004-P1-order-rankdelta.patch"
sed -i 's/^\t$(CC) -m64 -O3 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/\t$(CC) -m64 -O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/' makefile
grep -q -- '-O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off' makefile
left=$(grep -rn 'getenv' src --include='*.cpp' --include='*.h' | grep -v FX2_PRINT_VOCAB || true)
[[ -z "$left" ]] || { echo "FAIL: getenv left: $left" >&2; exit 1; }
rm -rf pgo_data && mkdir -p pgo_data
make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/profgen.log" 2>&1
env -i PATH="$PATH" HOME="$HOME" ./cmix -c dictionary/english.dic ./prof_input/input ./prof_comp > /dev/null 2>&1
rm -f prof_comp ppm.temp progress.log
make CFLAGS_DEFINES="$DEFS" prof_use > "$E/logs/profuse.log" 2>&1
dis=$(objdump -d --insn-width=16 cmix)
for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
  if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
done
if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
cp cmix "$E/bin/cmix"
python3 "$P/order_rankdelta.py" src/readalike_prepr/data/new_article_order "$E/bin/new_article_order.rd"
python3 "$P/mk_tfwc3.py" models/6m-q4-fp32.tfwc2 "$E/bin/6m-q4-fp32.tfwc3" "$E/tree"
"$P/build_stub.sh" "$E/bin/cmix" "$E/bin/cmix_packed"
R="$E/run"; rm -rf "$R"; mkdir -p "$R"; cp "$E/bin/cmix_packed" "$R/cmix_orig"
( cd "$R"
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/tree/dictionary/english.dic" ./comp_dict > /dev/null 2>&1
  env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$E/bin/new_article_order.rd" ./comp_order > /dev/null 2>&1
  cp "$E/bin/6m-q4-fp32.tfwc3" ./comp_tfweights
  ./cmix_orig -h "$(wc -c < comp_dict)" "$(wc -c < comp_order)" 0 "$(wc -c < comp_tfweights)"
  cat cmix_orig comp_dict comp_order comp_tfweights header.dat > comp9 && chmod +x comp9
  rm -f ppm.temp progress.log *.cmix.temp
  wc -c cmix_orig comp_dict comp_order comp_tfweights header.dat comp9 | tee "$E/bin/comp9.parts" )
cp "$R/comp9" "$E/bin/"
echo "built $E/bin/{cmix,cmix_packed,comp9}"
