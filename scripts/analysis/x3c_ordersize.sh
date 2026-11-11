#!/usr/bin/env bash
# X3c: the real coded size of an article-order stream: `cmix -c <order.rd> out` with the C4 binary, exactly as the
# build makes comp_order (patches/exp/C4-combo/build.sh), one fast-lane slot per file (cmix holds ~5 GB).
#   x3c_ordersize.sh W...     -> build/exp/X3c/orders/order_wW.rd.size  (W = 1 must give 181,132)
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; cd "$REPO"
B="$REPO/build/exp/C4/bin/cmix"; O="$REPO/build/exp/X3c/orders"
for w in "$@"; do
  d="$O/run_w$w"; rm -rf "$d"; mkdir -p "$d"
  ( cd "$d" && "$REPO/scripts/slot.sh" --fast env -i PATH=/usr/bin:/bin "$B" -c "$O/order_w$w.rd" comp_order > log 2>&1 )
  stat -c %s "$d/comp_order" > "$O/order_w$w.rd.size"; echo "W=$w $(cat "$O/order_w$w.rd.size")"
  rm -rf "$d"
done
