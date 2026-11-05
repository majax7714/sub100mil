#!/usr/bin/env bash
# Fetch third-party reference compressors into third_party/, pinned to exact commits.
# Never vendored into git (third_party/ is gitignored). Idempotent.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party

clone() { # name url commit
  local name=$1 url=$2 commit=$3 dir="third_party/$1"
  [ -d "$dir/.git" ] || git clone --quiet "$url" "$dir"
  git -C "$dir" -c advice.detachedHead=false checkout --quiet "$commit"
  echo "$name @ $(git -C "$dir" rev-parse HEAD)"
}

# Current Hutter Prize record (100,424,672 B, awarded 2026-09-30). GPL-3.0
clone fx2-cmix-transformer-v1 https://github.com/astOwOlfo/fx2-cmix-transformer-v1 83f2603f3f7751da5f429fd32669807eb8510494
# Pending entry (95,914,051 B). GPL-3.0-only
clone cmix-neif-1.0 https://github.com/marwoelk/cmix-neif-1.0 1c9ea5ed57ec9718bcb681d20b79e81a60b7a728
# neif + online fast-weight head (LTCB-only, 95,773,359 B); read for E2 (patches/exp/E2-correction-head). GPL-3.0
clone ex-girlfriend https://github.com/rlanday/ex-girlfriend e923faec085a93d24bf519986ee2881497e97934
