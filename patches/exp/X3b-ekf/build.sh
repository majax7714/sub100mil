#!/usr/bin/env bash
# X3b build = the C4 submission tree (patches/exp/C4-combo/mktree.sh) + x3b_ekf.h + 0001-X3b-ekf-after-G1.patch.
# Same pipeline as C4's build.sh step 1 (g++ PGO+LTO, -march=x86-64-v3 from the makefile, the record's guards,
# PGO trained with the dictionary under env -i). Output: build/exp/X3b/bin/cmix (unpacked) and cmix_packed (p1cm stub).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E="$REPO/build/exp/X3b"; H="$REPO/patches/exp/X3b-ekf"; P="$REPO/patches/exp/P1-packaging"; T="$E/tree"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000 -DE1CONF -DE1_GATE=3 -fno-asynchronous-unwind-tables -fno-unwind-tables"
mkdir -p "$E/bin" "$E/logs"
rm -rf "$T"
"$REPO/patches/exp/C4-combo/mktree.sh" "$T"
( cd "$T"
  cp "$H/x3b_ekf.h" src/models/x3b_ekf.h
  git apply "$H/0001-X3b-ekf-after-G1.patch"
  sed -i 's/^\t$(CC) -m64 -O3 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/\t$(CC) -m64 -O2 -std=c++17 -Wall -fno-math-errno -ffp-contract=off/' makefile
  rm -rf pgo_data && mkdir -p pgo_data
  make CFLAGS_DEFINES="$DEFS" prof_gen > "$E/logs/profgen.log" 2>&1
  env -i PATH="$PATH" HOME="$HOME" ./cmix -c dictionary/english.dic ./prof_input/input ./prof_comp > "$E/logs/train.log" 2>&1
  grep -q '^X3b: EKF combiner on' "$E/logs/train.log" || { echo "FAIL: PGO training did not run X3b" >&2; exit 1; }
  rm -f prof_comp ppm.temp progress.log
  make CFLAGS_DEFINES="$DEFS" prof_use > "$E/logs/profuse.log" 2>&1
  dis=$(objdump -d --insn-width=16 cmix)
  for pat in '%zmm' '%k[0-7]' '\{vex\}' $'\t62 [0-9a-f][0-9a-f] ' '\b(v?rcpps|v?rcpss|v?rsqrtps|v?rsqrtss)\b'; do
    if grep -qE "$pat" <<< "$dis"; then echo "FAIL: cmix matches '$pat'" >&2; exit 1; fi
  done
  if nm -D cmix | grep -q '_ZGV'; then echo "FAIL: cmix uses glibc libmvec" >&2; exit 1; fi
  cp cmix "$E/bin/cmix" )
"$P/build_stub.sh" "$E/bin/cmix" "$E/bin/cmix_packed"
ls -l "$E/bin/cmix" "$E/bin/cmix_packed" "$REPO/build/exp/C4/bin/cmix" "$REPO/build/exp/C4/bin/cmix_packed"
