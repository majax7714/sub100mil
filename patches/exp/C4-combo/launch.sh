#!/usr/bin/env bash
# launch.sh: start the C4 full-enwik9 round trip fully detached (setsid nohup) through the main lane and write
# build/exp/FULL-C4/STATUS.md. Modelled on patches/exp/C3-full/launch.sh. Compress = bin/comp9_costlog -e (per-byte
# cost log to build/exp/FULL-C4/cost_full.bin), decompress = the submission archive9 (bin/cmix_packed, P1's packed exe,
# as its decompressor), cmp + ledger row by scripts/bench.sh. S = archive9 + the packed comp9 (bin/comp9).
# DO NOT run this before the orchestrator decides (after the C3 full run verifies). Needs a free main-lane slot and
# about 30 GB of disk (cost log 23.5 GB, -e intermediates ~4 GB, decomp 1 GB). Peak RSS forecast: C3's (~9.75-9.8e9 B)
# + ~20 MB (T1 titles + G1); check the C3 run's measured peak first.
# Commit first: bench.sh reads HEAD only when the run ENDS (60+ h later), so the launch commit goes into --notes.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
F="$REPO/build/exp/FULL-C4"; B="$REPO/build/exp/C4/bin"
[[ -e "$F" ]] && { echo "$F exists: move it away first (a launched run?)" >&2; exit 1; }
for f in comp9 comp9_costlog cmix_packed; do [[ -x "$B/$f" ]] || { echo "missing $B/$f: run patches/exp/C4-combo/build.sh" >&2; exit 1; }; done
[[ -z "$(git status --porcelain --untracked-files=no -- patches/exp/C4-combo patches/exp/C3-full/full_cmd.sh)" ]] \
  || { echo "commit patches/exp/C4-combo first" >&2; exit 1; }
mkdir -p "$F/tmp"
C=$(git rev-parse --short HEAD); S9=$(stat -c %s "$B/comp9")
COMP="FX2_COSTLOG=$F/cost_full.bin patches/exp/C4-combo/full_cmd.sh comp {in} {out}"
DECOMP="patches/exp/C4-combo/full_cmd.sh decomp {in} {out}"
METHOD="fx2T+C4-full (comp9 -e costlog twin; clean packed archive9)"
NOTES="C4 submission build patches/exp/C4-combo @ $C (switches baked, env -i): C3 (C2 [C1 + E12 lr 0.004] + X2) + T1 interlanguage title copy (lr 0.004) + G1 GLN after the BITLSTM head (n8 k8, z3 = BITLSTM delta) + P1 packaging (p1cm stub, regex-free E12, FX2TFWC3, rank-delta order); full mode: compress = comp9_costlog -e (per-byte cost log, observation-only), output = archive9 with the packed clean decompressor swapped in, decompress = that ./archive9; comp9 = packed clean comp9 ($S9 B); timing contended"
env BENCH_TMP="$F/tmp" setsid nohup scripts/slot.sh scripts/bench.sh --method "$METHOD" --slice data/raw/enwik9 \
  --comp "$COMP" --decomp "$DECOMP" --decomp-size "$S9" --keep --notes "$NOTES" > "$F/run.log" 2>&1 < /dev/null &
PID=$!; sleep 5
cat > "$F/STATUS.md" <<EOT
# C4 full enwik9 run: status

- **Launched:** $(date '+%F %T %Z') from commit \`$C\` (patches/exp/C4-combo/launch.sh). Detached (setsid nohup).
- **PIDs:** launcher/slot.sh $PID (session leader; \`ps -s $PID -o pid,etime,rss,args\` lists the whole tree).
- **Expected:** compress then decompress; C3's run is the reference for wall time (60-90+ h total contended).
  T1 + G1 add about 0.3 h per direction (ESTIMATE), the packed exe about 3 s per start.

## Exact command
\`\`\`sh
cd $REPO
env BENCH_TMP=$F/tmp setsid nohup scripts/slot.sh scripts/bench.sh --method '$METHOD' --slice data/raw/enwik9 \\
  --comp '$COMP' \\
  --decomp '$DECOMP' \\
  --decomp-size $S9 --keep --notes '<see results/ledger.tsv>' > $F/run.log 2>&1 < /dev/null &
\`\`\`

## Files
- \`$F/run.log\`: slot.sh + bench.sh output (summary at the end: sizes, times, peak RSS, round-trip).
- \`$F/tmp/bench.*/\`: the bench work dir (kept). During compress: \`comp_run/\`; after: \`comp.bin\` = the SUBMISSION
  archive9 (packed decompressor), \`comp.sizes\`, \`comp.stdout/stderr\`, \`comp.progress.log\`. During decompress:
  \`decomp_run/\`; after: \`decomp.out\` (1 GB), \`decomp.stdout/stderr\`.
- \`$F/cost_full.bin\`: per-byte cost log (16 B header + 40 B per coded byte, ~23.5 GB when done; coded p after G1).
- Ledger row: appended by bench.sh at the end (method \`$METHOD\`).

## How to check progress
\`\`\`sh
W=\$(ls -d $F/tmp/bench.* | tail -1)
tail -1 \$W/comp_run/progress.log            # compress phase
ls -l $F/cost_full.bin                       # grows 40 B per coded byte (÷ 586,459,020×40 = fraction done)
tr '\\r' '\\n' < \$W/decomp.stdout | tail -1   # decompress phase
ps -s $PID -o pid,etime,rss,args             # alive? RSS in kB (watch against 10 GB)
tail $F/run.log                              # final summary
\`\`\`
Do not wait with \`until ! pgrep -f ...\` (it matches itself); use \`while kill -0 <pid>; do sleep 60; done\`.
Nothing is resumable: the Encoder keeps the whole archive in memory until the end.
EOT
echo "launched: PID $PID; status in $F/STATUS.md"
