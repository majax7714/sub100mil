#!/usr/bin/env bash
# mktree.sh TREE: C4's source tree (patches/exp/C4-combo/mktree.sh), committed as "C4" inside TREE's own git repo,
# then this directory's 0001-x2fix.patch (src/models/x2_timelaw.h only). Used by build.sh.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; P="$REPO/patches/exp"; T="$(realpath -m "$1")"
"$P/C4-combo/mktree.sh" "$T"
cd "$T"
git add -A && git -c user.name=build -c user.email=build@local commit -qm C4
git apply "$P/C4-x2fix/0001-x2fix.patch"
