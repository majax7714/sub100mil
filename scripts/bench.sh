#!/usr/bin/env bash
# Standard benchmark harness: compress, decompress, verify, record.
#
# Usage:
#   scripts/bench.sh --method NAME --slice PATH --comp 'CMD' --decomp 'CMD' \
#                    [--threads N] [--notes TEXT] [--keep] [--no-ledger] \
#                    [--decomp-size BYTES] [--extra-bytes N]
#
# CMD templates use {in} and {out} placeholders and run via `bash -c` from the
# caller's working directory, so redirects and pipes work:
#   --comp   'xz -9e -T1 -c {in} > {out}'
#   --decomp 'xz -d -c {in} > {out}'
# For comp, {in} is the slice and {out} is <workdir>/comp.bin.
# For decomp, {in} is <workdir>/comp.bin and {out} is <workdir>/decomp.out.
#
# Each step runs under /usr/bin/time -v (wall seconds, peak RSS). The
# decompressed file is cmp'd against the slice. A row is appended to
# results/ledger.tsv (unless --no-ledger), also when a step or the round-trip
# fails (roundtrip_ok=no). Exit status is 0 only if everything succeeded.
#
# --decomp-size / --extra-bytes are only recorded in the notes column;
# output_bytes is always the size of the compressed file.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LEDGER="$REPO_ROOT/results/ledger.tsv"
LEDGER_HEADER=$'date\tcommit\tmethod\tslice\tinput_bytes\toutput_bytes\tbpb\tcomp_s\tdecomp_s\tpeak_rss_mb\tthreads\troundtrip_ok\tnotes'
BENCH_TMP="${BENCH_TMP:-/tmp/claude-1000/-home-mmarrujo-sub100mil/9d3b9113-c9c6-4d39-ac90-a720c8f61d37/scratchpad}"

die()   { printf '[bench] ERROR: %s\n' "$*" >&2; exit 2; }
usage() { sed -n '2,/^set -uo/{/^set -uo/d;s/^# \{0,1\}//;p}' "${BASH_SOURCE[0]}"; exit "${1:-2}"; }

METHOD="" SLICE="" COMP="" DECOMP="" THREADS=1 NOTES="" KEEP=0 LEDGER_ON=1
DECOMP_SIZE="" EXTRA_BYTES=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --method)      METHOD="${2-}"; shift 2 ;;
    --slice)       SLICE="${2-}"; shift 2 ;;
    --comp)        COMP="${2-}"; shift 2 ;;
    --decomp)      DECOMP="${2-}"; shift 2 ;;
    --threads)     THREADS="${2-}"; shift 2 ;;
    --notes)       NOTES="${2-}"; shift 2 ;;
    --decomp-size) DECOMP_SIZE="${2-}"; shift 2 ;;
    --extra-bytes) EXTRA_BYTES="${2-}"; shift 2 ;;
    --keep)        KEEP=1; shift ;;
    --no-ledger)   LEDGER_ON=0; shift ;;
    -h|--help)     usage 0 ;;
    *)             die "unknown argument: $1 (see --help)" ;;
  esac
done

[[ -n "$METHOD" && -n "$SLICE" && -n "$COMP" && -n "$DECOMP" ]] \
  || die "--method, --slice, --comp and --decomp are required (see --help)"
[[ -f "$SLICE" ]] || die "slice not found: $SLICE"
[[ "$THREADS" =~ ^[0-9]+$ ]] || die "--threads must be an integer"
for v in "$DECOMP_SIZE" "$EXTRA_BYTES"; do
  [[ -z "$v" || "$v" =~ ^[0-9]+$ ]] || die "--decomp-size/--extra-bytes must be integers"
done
[[ -x /usr/bin/time ]] || die "/usr/bin/time not found (dnf install time)"

SLICE_ABS="$(realpath "$SLICE")"
SLICE_NAME="$(basename "$SLICE")"
INPUT_BYTES="$(stat -L -c %s "$SLICE_ABS")"

mkdir -p "$BENCH_TMP" || die "cannot create $BENCH_TMP"
WORK="$(mktemp -d "$BENCH_TMP/bench.XXXXXX")" || die "mktemp failed"
cleanup() { if [[ $KEEP == 1 ]]; then printf '[bench] kept work dir: %s\n' "$WORK" >&2; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
trap 'exit 130' INT TERM

COMP_OUT="$WORK/comp.bin"
DECOMP_OUT="$WORK/decomp.out"

# fill_template TEMPLATE IN OUT -> command string with shell-quoted paths
fill_template() {
  local t="$1" qin qout
  qin="$(printf '%q' "$2")"; qout="$(printf '%q' "$3")"
  t="${t//\{in\}/$qin}"
  printf '%s' "${t//\{out\}/$qout}"
}

# run_timed LABEL CMD -> sets RUN_RC, RUN_S (wall seconds), RUN_RSS_KB
run_timed() {
  local label="$1" cmd="$2" tf="$WORK/$1.time"
  printf '[bench] %s: %s\n' "$label" "$cmd" >&2
  /usr/bin/time -v -o "$tf" bash -c "$cmd"
  RUN_RC=$?
  # Elapsed is "m:ss.ss" or "h:mm:ss"
  RUN_S="$(awk -F': ' '/Elapsed \(wall clock\)/ {
      n = split($2, p, ":"); s = 0; for (i = 1; i <= n; i++) s = s * 60 + p[i]
      printf "%.2f", s }' "$tf" 2>/dev/null)"
  RUN_RSS_KB="$(awk -F': ' '/Maximum resident set size/ {print $2}' "$tf" 2>/dev/null)"
  RUN_RSS_KB="${RUN_RSS_KB:-0}"
}

fsize() { [[ -f "$1" ]] && stat -c %s "$1" || echo ""; }

FAIL_REASON=""
COMP_S="" DECOMP_S="" COMP_RSS=0 DECOMP_RSS=0 OUTPUT_BYTES="" RT_OK=no

# 1. compress
run_timed comp "$(fill_template "$COMP" "$SLICE_ABS" "$COMP_OUT")"
COMP_S="$RUN_S"; COMP_RSS="$RUN_RSS_KB"
OUTPUT_BYTES="$(fsize "$COMP_OUT")"
if [[ $RUN_RC != 0 ]]; then
  FAIL_REASON="comp exited $RUN_RC"
elif [[ -z "$OUTPUT_BYTES" ]]; then
  FAIL_REASON="comp produced no output file"
fi

# 2. decompress
if [[ -z "$FAIL_REASON" ]]; then
  run_timed decomp "$(fill_template "$DECOMP" "$COMP_OUT" "$DECOMP_OUT")"
  DECOMP_S="$RUN_S"; DECOMP_RSS="$RUN_RSS_KB"
  if [[ $RUN_RC != 0 ]]; then
    FAIL_REASON="decomp exited $RUN_RC"
  elif [[ ! -f "$DECOMP_OUT" ]]; then
    FAIL_REASON="decomp produced no output file"
  fi
fi

# 3. verify
if [[ -z "$FAIL_REASON" ]]; then
  if cmp_msg="$(cmp "$SLICE_ABS" "$DECOMP_OUT" 2>&1)"; then
    RT_OK=yes
  else
    cmp_msg="${cmp_msg//"$SLICE_ABS"/original}"; cmp_msg="${cmp_msg//"$DECOMP_OUT"/decompressed}"
    FAIL_REASON="cmp mismatch: ${cmp_msg:-differs}; decompressed $(fsize "$DECOMP_OUT") of $INPUT_BYTES bytes"
  fi
fi

# 4. summary
BPB=""
[[ -n "$OUTPUT_BYTES" ]] && BPB="$(awk -v o="$OUTPUT_BYTES" -v i="$INPUT_BYTES" 'BEGIN{printf "%.4f", 8*o/i}')"
PEAK_KB=$(( COMP_RSS > DECOMP_RSS ? COMP_RSS : DECOMP_RSS ))
kb2mb() { awk -v k="$1" 'BEGIN{printf "%.1f", k/1024}'; }
PEAK_MB="$(kb2mb "$PEAK_KB")"

COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [[ "$COMMIT" != unknown && -n "$(git -C "$REPO_ROOT" status --porcelain --untracked-files=no -- . ':(exclude)results/ledger.tsv' 2>/dev/null)" ]]; then
  COMMIT="$COMMIT+dirty"
fi

ROW_NOTES="$NOTES"
add_note() { ROW_NOTES="${ROW_NOTES:+$ROW_NOTES; }$1"; }
[[ -n "$DECOMP_SIZE" ]] && add_note "decomp_size=$DECOMP_SIZE"
[[ -n "$EXTRA_BYTES" ]] && add_note "extra_bytes=$EXTRA_BYTES"
[[ -n "$FAIL_REASON" ]] && add_note "FAIL: $FAIL_REASON"
ROW_NOTES="$(printf '%s' "$ROW_NOTES" | tr '\t\n\r' '   ')"
METHOD="$(printf "%s" "$METHOD" | tr "\t\n\r" "   ")"

{
  echo "================ bench summary ================"
  printf '%-14s %s\n' method "$METHOD"
  printf '%-14s %s\n' slice "$SLICE_NAME"
  printf '%-14s %s\n' commit "$COMMIT"
  printf '%-14s %s\n' input_bytes "$INPUT_BYTES"
  printf '%-14s %s\n' output_bytes "${OUTPUT_BYTES:-n/a}"
  [[ -n "$DECOMP_SIZE" ]] && printf '%-14s %s (total S = %s)\n' decomp_size "$DECOMP_SIZE" \
    "$(( ${OUTPUT_BYTES:-0} + DECOMP_SIZE + ${EXTRA_BYTES:-0} ))"
  [[ -n "$EXTRA_BYTES" ]] && printf '%-14s %s\n' extra_bytes "$EXTRA_BYTES"
  printf '%-14s %s\n' bpb "${BPB:-n/a}"
  printf '%-14s %s\n' comp_s "${COMP_S:-n/a}"
  printf '%-14s %s\n' decomp_s "${DECOMP_S:-n/a}"
  printf '%-14s %s (comp %s, decomp %s)\n' peak_rss_mb "$PEAK_MB" "$(kb2mb "$COMP_RSS")" "$(kb2mb "$DECOMP_RSS")"
  printf '%-14s %s\n' threads "$THREADS"
  if [[ $RT_OK == yes ]]; then
    printf '%-14s %s\n' roundtrip OK
  else
    printf '%-14s FAIL (%s)\n' roundtrip "$FAIL_REASON"
  fi
  echo "==============================================="
}

# 5. ledger
if [[ $LEDGER_ON == 1 ]]; then
  if [[ ! -s "$LEDGER" ]]; then
    mkdir -p "$(dirname "$LEDGER")"
    printf '%s\n' "$LEDGER_HEADER" > "$LEDGER"
  elif [[ "$(head -n1 "$LEDGER")" != "$LEDGER_HEADER" ]]; then
    die "ledger header in $LEDGER does not match expected columns; not writing"
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$(date +%F)" "$COMMIT" "$METHOD" "$SLICE_NAME" "$INPUT_BYTES" "$OUTPUT_BYTES" \
    "$BPB" "$COMP_S" "$DECOMP_S" "$PEAK_MB" "$THREADS" "$RT_OK" "$ROW_NOTES" >> "$LEDGER"
  printf '[bench] appended row to %s\n' "${LEDGER#$REPO_ROOT/}"
else
  printf '[bench] --no-ledger: not recording\n'
fi

[[ $RT_OK == yes ]] || exit 1
exit 0
