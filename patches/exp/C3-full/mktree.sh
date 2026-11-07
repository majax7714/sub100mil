#!/usr/bin/env bash
# mktree.sh TREE: recreate C3's source tree (patches/exp/C3-combo/build.sh steps 1-10, same order) at TREE, commit
# it as "C3" inside TREE's own git repo, then apply this directory's 0001-C3-bake-defaults.patch (the submission
# switches baked in). Used by build.sh; keep the chain in sync with C3-combo/build.sh.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO/third_party/fx2-cmix-transformer-v1"; P="$REPO/patches"; T="$(realpath -m "$1")"
[[ "$(git -C "$SRC" rev-parse HEAD)" == 83f2603f3f7751da5f429fd32669807eb8510494 ]] || { echo "third_party not at 83f2603" >&2; exit 1; }
rm -rf "$T"; mkdir -p "$T"
rsync -a --exclude .git --exclude 'models/*.tch' --exclude 'cpp_infer/bench' "$SRC/" "$T/"
cd "$T"
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
git add -A && git -c user.name=build -c user.email=build@local commit -qm C3
[[ "$(md5sum dictionary/english.dic | awk '{print $1}')" == b3a9cf9fac570b2dc9a0375534792980 ]]
if [[ -f "$P/exp/C3-full/0001-C3-bake-defaults.patch" && "${C3_NO_BAKE-}" != 1 ]]; then
  git apply "$P/exp/C3-full/0001-C3-bake-defaults.patch"
fi
