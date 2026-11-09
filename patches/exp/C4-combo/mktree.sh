#!/usr/bin/env bash
# mktree.sh TREE: C4's source tree = C3-full's (patches/exp/C3-full/mktree.sh: C3 + 0001-C3-bake-defaults) + the
# T1/G1 model files + G1's coder hooks + P1's size patches, committed as "base" inside TREE's own git repo, then
# this directory's 0001-C4-T1-G1-baked.patch (T1's and G1's predictor lines, baked: no env; WRT decoder shared by X2
# and T1; G1's z[3] = the BITLSTM delta). Used by build.sh.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; P="$REPO/patches/exp"; T="$(realpath -m "$1")"
"$P/C3-full/mktree.sh" "$T"
cd "$T"
git add -A && git -c user.name=build -c user.email=build@local commit -qm C3-full
cp "$P/T1-fieldcopy/t1_fieldcopy.h" src/models/t1_fieldcopy.h
cp "$P/G1-gln/g1_gln.h" src/models/g1_gln.h
git apply --exclude='src/predictor.*' "$P/G1-gln/0001-G1-gln-final-stage.patch"   # coder hooks only
cp "$P/P1-packaging/e12_re.h" src/models/e12_re.h
git apply "$P/P1-packaging/0001-P1-e12-no-regex.patch"
git apply "$P/P1-packaging/0002-P1-tfwc3-weights.patch"
cp "$P/P1-packaging/order_rankdelta.h" src/readalike_prepr/order_rankdelta.h
git apply "$P/P1-packaging/0004-P1-order-rankdelta.patch"
git add -A && git -c user.name=build -c user.email=build@local commit -qm base
git apply "$P/C4-combo/0001-C4-T1-G1-baked.patch"
