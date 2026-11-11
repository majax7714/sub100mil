#!/usr/bin/env bash
# smoke.sh DIR [CODE_MIN]: full-mode check of the X3a build without a full run (part 1 of patches/exp/C4-combo/smoke.sh).
# Assembles comp9 = X3a's packed exe + C4's comp_dict + X3a's comp_order (the residual) + C4's weights + header, runs
# `comp9 -e enwik9` under env -i until CODE_MIN minutes after the coder starts, then kills it. Checks that the packed exe
# self-extracts the dictionary and the residual, that reorder() rebuilds the order (order_rule.h), and that
# .main_reordered, .ready4cmix and .dict are byte-identical to the record's -e run (build/exp/E12/full).
# Logs the session RSS each minute. Hold a main-lane slot: scripts/slot.sh patches/exp/X3a-order-rule/smoke.sh DIR 1
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; B="${X3A_BIN:-$REPO/build/exp/X3a/c4x/bin}"; C4="$REPO/build/exp/C4/run"
REF="$REPO/build/exp/E12/full"; D="$(realpath -m "$1")"; CM=${2:-1}; L="$D/smoke.log"
rm -rf "$D"; mkdir -p "$D/comp_run" "$D/asm"
log() { echo "$(date +%T) $*" | tee -a "$L"; }
( cd "$D/asm"; cp "$B/cmix_packed" cmix_orig
  ./cmix_orig -h "$(stat -c %s "$C4/comp_dict")" "$(stat -c %s "$B/comp_order")" 0 "$(stat -c %s "$C4/comp_tfweights")" > /dev/null
  cat cmix_orig "$C4/comp_dict" "$B/comp_order" "$C4/comp_tfweights" header.dat > comp9; chmod +x comp9 )
log "comp9 $(stat -c %s "$D/asm/comp9") B (C4: $(stat -c %s "$C4/comp9") B)"
cd "$D/comp_run"; cp "$D/asm/comp9" cmix
setsid env -i PATH=/usr/bin:/bin HOME="$HOME" ./cmix -e "$REPO/data/raw/enwik9" payload > "$D/comp.stdout" 2> "$D/comp.stderr" &
pid=$!; sleep 3; t0=""; n=0; peak=0
sid=$(ps -o sid= -p "$pid" | tr -d " ")
[[ "$sid" == "$pid" ]] || { log "FAIL: $pid is not a session leader (sid $sid)"; pkill -P "$pid"; kill "$pid"; exit 1; }
while kill -0 "$pid" 2>/dev/null; do
  tot=0; for p in $(pgrep -s "$pid"); do tot=$((tot + $(awk '/VmRSS/{print $2}' /proc/"$p"/status 2>/dev/null || echo 0))); done
  (( tot > peak )) && peak=$tot
  log "session rss $tot kB (peak $peak) | $(ls -A . | tr '\n' ' ' | tail -c 160)"
  if [[ -z "$t0" && -s progress.log ]]; then t0=$n; log "coder running"; fi
  if [[ -n "$t0" && $((n - t0)) -ge $CM ]] || (( n >= 45 )); then log "killing"; pkill -s "$pid"; sleep 5; pkill -9 -s "$pid" 2>/dev/null; break; fi
  sleep 60; n=$((n + 1))
done
wait "$pid" 2>/dev/null; log "process ended"
for f in .main_reordered .ready4cmix .dict; do
  if cmp -s "$D/comp_run/$f" "$REF/$f"; then log "IDENTICAL to the record's -e: $f ($(stat -c %s "$REF/$f") B)"
  else log "DIFFERS from the record's -e: $f ($(stat -c %s "$D/comp_run/$f" 2>/dev/null) vs $(stat -c %s "$REF/$f") B)"; fi
done
if cmp -s "$D/comp_run/.new_article_order" "$B/new_article_order.res"; then log "self-extracted order block = the residual ($(stat -c %s "$B/new_article_order.res") B)"; else log "order block DIFFERS from the residual"; fi
tr '\r' '\n' < "$D/comp.stderr" | grep -E '^(order rule|E12|X2|T1|G1)' | sort | uniq -c | tee -a "$L"
rm -f ppm.temp .main .main_phda9prepr un1 .main_reordered .ready4cmix
