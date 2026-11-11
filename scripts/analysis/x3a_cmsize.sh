#!/usr/bin/env bash
# X3a: CM-code a file exactly as the comp9 construct step codes the article order (P1's build_c3full_p1.sh:
# `env -i ./cmix_orig -c FILE comp_order` with the packed C3-full+P1 exe) and print its size.
#   scripts/analysis/x3a_cmsize.sh FILE...     (run through scripts/slot.sh --fast; about 6 GB RAM, ~200 us/byte)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXE=${X3A_EXE:-$ROOT/build/exp/P1/c3full_p1/bin/cmix_packed}
for f in "$@"; do
  f=$(realpath "$f"); R=$(mktemp -d "$ROOT/build/exp/X3a/cm.XXXXXX"); cp "$EXE" "$R/cmix_orig"
  ( cd "$R"; env -i PATH="$PATH" HOME="$HOME" ./cmix_orig -c "$f" ./comp_order > /dev/null 2>&1 || true
    printf '%s\t%s\t%s\n' "$(wc -c < comp_order)" "$(wc -c < "$f")" "$f" )
  rm -rf "$R"
done
