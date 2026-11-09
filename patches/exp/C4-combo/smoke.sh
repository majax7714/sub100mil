#!/usr/bin/env bash
# smoke.sh DIR [CODE_MIN]: full-mode readiness of the C4 SUBMISSION binaries without a full run (full mode is
# enwik9-only). Hold a main-lane slot around it: scripts/slot.sh patches/exp/C4-combo/smoke.sh DIR 3
#  1. the packed clean comp9 (bin/comp9) -e enwik9 in DIR/comp_run under env -i, until CODE_MIN minutes after the coder
#     starts (progress.log appears), logging RSS each minute; then kill. Checks: the packed exe self-extracts (stub ->
#     memfd exec -> opens ./cmix), the dictionary and the RANK-DELTA order (P1 0004) decode, and the reordered /
#     preprocessed streams (.main_reordered, .ready4cmix) are byte-identical to the record's -e run
#     (build/exp/E12/full, upstream's order file), so the order change is exact; E12/X2/T1/G1 are built on ./.dict
#     with the coded block header (stderr).
#  2. a submission-layout archive9 with the packed exe: cmix_packed + .dict.comp + .tfweights + payload + header, run
#     under env -i in DIR/dec: the packed decompressor self-extracts and verifies the dictionary and builds
#     E12/X2/T1/G1 on ./.dict. A killed -e leaves an EMPTY payload (the Encoder holds its output until Flush), so it then
#     fails on the empty payload, as expected (same limit as patches/exp/C3-full/smoke.sh).
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; B="$REPO/build/exp/C4/bin"; REF="$REPO/build/exp/E12/full"
D="$(realpath -m "$1")"; CM=${2:-3}; L="$D/smoke.log"
rm -rf "$D"; mkdir -p "$D/comp_run" "$D/dec"
log() { echo "$(date +%T) $*" | tee -a "$L"; }
watch_pid() {  # watch_pid PID DIR PROGRESSFILE MINUTES (MINUTES < 0: no kill)
  local pid=$1 dir=$2 prog=$3 mins=$4 t0="" n=0 peak=0 sid; sid=$(ps -o sid= -p "$pid" | tr -d " ")
  [[ "$sid" == "$pid" ]] || { log "FAIL: $pid is not a session leader (sid $sid); not killing by session"; sid=""; }
  while kill -0 "$pid" 2>/dev/null; do
    local tot=0 p
    for p in $( [[ -n "$sid" ]] && pgrep -s "$sid" || echo "$pid"); do
      tot=$((tot + $(awk '/VmRSS/{print $2}' /proc/"$p"/status 2>/dev/null || echo 0)))
    done
    (( tot > peak )) && peak=$tot
    local pr=""; [[ -f "$dir/$prog" ]] && pr=$(tail -1 "$dir/$prog" 2>/dev/null | tr '\r' ' ' | tail -c 80)
    log "pid $pid session rss $tot kB (peak $peak) | progress: ${pr:-none} | $(ls -A "$dir" | tr '\n' ' ' | tail -c 200)"
    if [[ -z "$t0" && -s "$dir/$prog" ]]; then t0=$n; log "coder running"; fi
    if [[ $mins -ge 0 && -n "$t0" && $((n - t0)) -ge $mins ]]; then log "killing after $mins min of coding"; { [[ -n "$sid" ]] && pkill -s "$sid" || kill "$pid"; }; sleep 5; { [[ -n "$sid" ]] && pkill -9 -s "$sid" || kill -9 "$pid"; } 2>/dev/null; break; fi
    if (( n >= 40 )); then log "killing after 40 min (hard cap)"; { [[ -n "$sid" ]] && pkill -s "$sid" || kill "$pid"; }; sleep 5; { [[ -n "$sid" ]] && pkill -9 -s "$sid" || kill -9 "$pid"; } 2>/dev/null; break; fi
    sleep 60; n=$((n + 1))
  done
  wait "$pid" 2>/dev/null; log "process ended (rc $?)"
}
cd "$D/comp_run"; cp "$B/comp9" cmix
setsid env -i PATH=/usr/bin:/bin HOME="$HOME" ./cmix -e "$REPO/data/raw/enwik9" payload > "$D/comp.stdout" 2> "$D/comp.stderr" &
PID=$!; sleep 3; watch_pid $PID "$D/comp_run" progress.log "$CM"
log "comp stderr (model lines):"; tr '\r' '\n' < "$D/comp.stderr" | grep -E '^(E12|X2|T1|G1|E2|BITL)' | sort | uniq -c | tee -a "$L"
ls -la "$D/comp_run" | tee -a "$L"
for f in .main_reordered .ready4cmix .new_article_order .dict; do
  if cmp -s "$D/comp_run/$f" "$REF/$f"; then log "IDENTICAL to the record's -e: $f ($(stat -c %s "$REF/$f") B)"
  else log "DIFFERS from the record's -e: $f ($(stat -c %s "$D/comp_run/$f" 2>/dev/null) vs $(stat -c %s "$REF/$f") B)"; fi
done
# 2. the submission archive9 layout with the packed exe
P="$D/comp_run/payload"; touch "$P"; cd "$D/dec"
cp "$B/cmix" hx; ./hx -h "$(stat -c %s ../comp_run/.dict.comp)" 0 "$(stat -c %s "$P")" "$(stat -c %s ../comp_run/.tfweights)" >/dev/null
cat "$B/cmix_packed" ../comp_run/.dict.comp ../comp_run/.tfweights "$P" header.dat > archive9; chmod +x archive9; rm -f hx header.dat
setsid env -i PATH=/usr/bin:/bin HOME="$HOME" ./archive9 > "$D/dec.stdout" 2> "$D/dec.stderr" &
PID=$!; sleep 3; watch_pid $PID "$D/dec" .input_decomp.cmix.temp -1
log "dec stderr:"; tr '\r' '\n' < "$D/dec.stderr" | tail -12 | tee -a "$L"
log "dec stdout:"; tr '\r' '\n' < "$D/dec.stdout" | tail -6 | tee -a "$L"
rm -f "$D/comp_run/ppm.temp" "$D/dec/ppm.temp" "$D/comp_run/.main" "$D/comp_run/.main_phda9prepr" "$D/comp_run/un1"
