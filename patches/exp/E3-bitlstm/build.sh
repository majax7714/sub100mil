#!/usr/bin/env bash
# E3 (BITLSTM, online-only) on fx2-cmix-transformer-v1: set up build/exp/E3/tree (pinned third_party copy +
# patches/fx2-cmix-transformer/*.patch + 0001-bitlstm-online.patch) and build the release cmix with the same
# pipeline as patches/fx2-cmix-transformer/build.sh (g++ PGO+LTO, x86-64-v3, AVX-512/RCPPS/RSQRTPS/libmvec guards).
# Output: build/exp/E3/bin/cmix-<name> (default name: bitl). The head is configured at run time by BITL_* env
# (see README.md); the SAME environment must be set for compression and decompression.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"; E="$REPO/build/exp/E3"; V="${1:-bitl}"
DEFS="-DSEED=923 -DUPDATE_LIMIT=3000"
[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
mkdir -p "$E/bin" "$E/logs/$V"; rm -rf "$E/tree"
rsync -a --exclude .git --exclude 'models/*.tch' --exclude 'cpp_infer/bench' "$SRC/" "$E/tree/"
cd "$E/tree"
git init -q . && git add -A && git -c user.name=build -c user.email=build@local commit -qm pristine
for p in "$REPO"/patches/fx2-cmix-transformer/*.patch "$REPO"/patches/exp/E3-bitlstm/*.patch; do git apply "$p"; done
L="$E/logs/$V"
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
