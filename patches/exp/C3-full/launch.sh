#!/usr/bin/env bash
# launch.sh: start the C3 full-enwik9 round trip fully detached (setsid nohup) through the main lane and write
# build/exp/FULL/STATUS.md. Compress = bin/comp9_costlog -e (per-byte cost log to build/exp/FULL/cost_full.bin),
# decompress = the clean submission archive9 (bin/cmix as its decompressor), cmp + ledger row by scripts/bench.sh.
# Commit first: bench.sh reads HEAD only when the run ENDS (60+ h later), so the launch commit goes into --notes.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; cd "$REPO"
F="$REPO/build/exp/FULL"; B="$REPO/build/exp/C3-full/bin"; mkdir -p "$F/tmp"
[[ -z "$(git status --porcelain --untracked-files=no -- patches/exp/C3-full)" ]] || { echo "commit patches/exp/C3-full first" >&2; exit 1; }
C=$(git rev-parse --short HEAD); S9=$(stat -c %s "$B/comp9")
COMP="FX2_COSTLOG=$F/cost_full.bin patches/exp/C3-full/full_cmd.sh comp {in} {out}"
DECOMP="patches/exp/C3-full/full_cmd.sh decomp {in} {out}"
METHOD="fx2T+C3-full (comp9 -e costlog twin; clean archive9)"
NOTES="C3 submission build patches/exp/C3-full @ $C (switches baked, env -i): C2 (C1 [E1 gate3 + E2 logit a0 0.25 af 0.05 + E3 agg16 lr_e6 3000 accum 1 + E9] + E12 lr 0.004) + X2; full mode: compress = comp9_costlog -e (per-byte cost log, observation-only), output = archive9 with the clean decompressor swapped in, decompress = that ./archive9; comp9 = un-UPX'd clean comp9 ($S9 B); timing contended"
env BENCH_TMP="$F/tmp" setsid nohup scripts/slot.sh scripts/bench.sh --method "$METHOD" --slice data/raw/enwik9 \
  --comp "$COMP" --decomp "$DECOMP" --decomp-size "$S9" --keep --notes "$NOTES" > "$F/run.log" 2>&1 < /dev/null &
PID=$!; sleep 5
cat > "$F/STATUS.md" <<EOT
# C3 full enwik9 run: status

- **Launched:** $(date '+%F %T %Z') from commit \`$C\` (patches/exp/C3-full/launch.sh). Detached (setsid nohup).
- **PIDs:** launcher/slot.sh $PID (session leader; \`ps -s $PID -o pid,etime,rss,args\` lists the whole tree).
- **Expected:** compress then decompress, each about 30 h idle-box ESTIMATE, **60–90+ h total contended**
  (the smoke test coded at about 420–510 µs per coded byte under batch-4 load, i.e. ~70–80 h for compress alone; the
  uncontended slice rate is about 180–280 µs). Earliest finish ≈ $(date -d '+60 hours' '+%F %H:%M'), more likely ≈ $(date -d '+80 hours' '+%F %H:%M').

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
- \`$F/tmp/bench.*/\`: the bench work dir (kept). During compress: \`comp_run/\` (comp9 -e's cwd: \`progress.log\`,
  ppm.temp 14 GiB sparse, ~4 GB of -e intermediates); after: \`comp.bin\` = the SUBMISSION archive9,
  \`comp.sizes\`, \`comp.stdout/stderr\`, \`comp.progress.log\`. During decompress: \`decomp_run/\`; after:
  \`decomp.out\` (1 GB), \`decomp.stdout/stderr\`.
- \`$F/cost_full.bin\`: per-byte cost log (16 B header + 40 B per coded byte, 586,459,020 records ≈ 23.5 GB when done;
  patches/tools/per-byte-cost/costlog.py reads it).
- Ledger row: appended by bench.sh at the end (method \`$METHOD\`).

## How to check progress
\`\`\`sh
W=\$(ls -d $F/tmp/bench.* | tail -1)
tail -1 \$W/comp_run/progress.log            # "<percent of coded stream> <archive bytes so far>" (compress phase)
ls -l $F/cost_full.bin                       # grows 40 B per coded byte (÷ 586,459,020×40 = fraction done)
tr '\\r' '\\n' < \$W/decomp.stdout | tail -1   # decompress phase
ps -s $PID -o pid,etime,rss,args             # alive? RSS in kB (watch against 10 GB)
tail $F/run.log                              # final summary
\`\`\`
Do not wait with \`until ! pgrep -f ...\` (it matches itself); use \`while kill -0 <pid>; do sleep 60; done\`.
If the box reboots or the run dies, nothing is resumable: the Encoder keeps the whole archive in memory until the end.
EOT
echo "launched: PID $PID; status in $F/STATUS.md"
