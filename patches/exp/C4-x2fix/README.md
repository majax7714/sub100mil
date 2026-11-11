# C4-x2fix: X2's page-id tracking fixed (drift and out-of-bounds read)

Type: **bug fix to a model addition** (X2's redirect page-id law). Base: `patches/exp/C4-combo`. Date: 2026-11-11.
Found by X3c (`docs/design/X3c-arrangement.md` §7.1); the report is right in every detail.

## The bug (in `x2_timelaw.h` of X2-timelaw, C3-combo, C3-full and C4-combo: all use the one file, C4 via its patch)

- `end_line()` took a record's page-id delta only if `revision>` was the very next line. A page with a
  `<restrictions>` element has `restrictions>…` in between, so its delta was dropped: **1,013 pages in the full run,
  final drift +2,740,978, all 70,571 redirect-law slots on a drifted base** (`x3c_x2drift.py`, `x2fix_replay.py`).
- `try_rpid()` read `used_[prev_pid_ + 1]` with no lower bound. With another page order the tracked id goes negative:
  X3c's `A1_rec` sample reaches −297 at record 50 and C4 exits 139. The full run's order never went below 0.

## The fix (`0001-x2fix.patch`, `src/models/x2_timelaw.h` only; encoder and decoder share the code)

- A pending delta survives a `restrictions>` line, so the tracked id is exact (drift 0 on the full block).
- The bitmap index is bounded at both ends (`x < 0` counts as unused).
- No input can overflow the arithmetic: tracked id clamped to ±1e15, revision id ≤ 18 digits, timestamp day and
  second fields ≤ 9 digits (real data: 3 and 5). None of these changes anything on real data.
- Audit of the same pattern: nothing else found in `x2_timelaw.h`, `t1_fieldcopy.h` (group/title indices are checked
  against the queue size, classes < 12) or `wrt_dec.h`. `scripts/analysis/x2fix_fuzz.cpp` (hostile metadata text,
  libstdc++ bounds assertions + `-ftrapv`; the box has no libasan): C4's headers abort, the fixed ones pass 200 seeds.
- `fullstream_check` (C4's, built on the fixed tree, real 586 MB -e stream): no crash, 594 s, peak RSS 91.7 MB, X2
  slots time 154,985 / rpid 70,571 (as C4), T1 hash unchanged (`ff98f2d5724cee3f`), X2 hash changed as expected.

## Files

`mktree.sh TREE` (C4's tree + the patch), `build.sh` (C4's build with this tree; `build/exp/x2fix/bin/`),
`run.sh` (C4's bench wrapper; method `fx2T+C4-x2fix`). Analysis: `scripts/analysis/x2fix_replay.py` (the law replayed
over the full block, old and new tracking; drifted seeds), `x2fix_fullcost.py` (what the C3 full run paid per page-id
field), `x2fix_fuzz.cpp`.

## Size

Packed exe 200,960 → **201,056 (+96 B)**; unpacked 537,888 both; comp9 3,384,847 → 3,384,943. The exe is in comp9 and
archive9, so +192 B of S.

## Results (packed exe, env -i, bench.sh, ledger rows `fx2T+C4-x2fix` and `fx2T+C4-full` of 2026-11-11)

| input | C4 | C4 + fix | Δ | round trip |
|---|---:|---:|---:|---|
| enwik9_10M | 1,260,281 | 1,260,281 | 0 | OK |
| enwik9_1M | 126,132 | 126,132 | 0 | OK |
| enwik9_mid1M_pa | 29,471 | 29,471 | 0 | OK |
| X2 proxy ranks 5,000 (11 restrictions pages) | 39,598 | 39,598 | 0 | OK |
| X2 proxy ranks 100,000 (9) | 36,682 | 36,682 | 0 | OK |
| X2 proxy ranks 130,000 (0) | 24,134 | 24,134 | 0 | OK |
| X2 proxy ranks 175,000, redirects (5) | 25,379 | 25,379 | 0 | OK |
| X2 proxy ranks 200,000, redirects (0) | 29,656 | 29,656 | 0 | OK |
| X3c `A1_rec` (C4: exit 139, twice) | crash | **129,500** | | **OK** |
| mini full-run analogue, pages 0–2,999 (30 restrictions pages, 984 slots) | 34,684 | **34,598** | **−86** | OK |
| mini full-run analogue, pages 20,000–22,999 (39; 914 slots) | 36,975 | **36,886** | **−89** | OK |
| mini full-run analogue, pages 60,000–62,999 | 34,867 | 34,791 | (−76) | **FAIL for both: slicetool pre/post alone does not round-trip this sample (same byte 443,151); not counted** |

- **Zero on every slice and cold proxy, as it must be:** there the ids ahead of the tracked one are all unused, so
  the law's candidates are the deltas 1..64 whatever the drift. The drift only matters when earlier records have
  marked ids ahead, i.e. redirects in id order *after* their articles: the full run.
- **Mini full-run analogues** (`x3c_mksample.py --set page:A:B --order rec --stub`: 3,000 consecutive pages, the
  articles in the record's order, then the redirects in id order): −86 / −89 B = **0.70 / 0.78 bits per slot**.

## Full enwik9 [ESTIMATE]

- **What C3 got from the redirect law: about 0 of the 7.4 KB.** `x2_csim` on the three redirect proxies' cost dumps,
  C4's header seeded with the bitmap as the full run's drifted tracking left it (`x2fix_replay.py --seed-dir`): gain
  0.00 / −0.00 / −0.00 bits per slot at ranks 175K / 200K / 230K. Cross-check on the C3 full-run dump
  (`x2fix_fullcost.py`): C3 paid 3.06 / 3.40 / 3.54 bits per page-id field in those windows; the emulation says
  3.09 / 3.42 / 3.51. Law alone, replayed over all 70,571 slots: true delta is candidate 0 in 34.7% (old) vs 57.6%
  (fixed), not a candidate at all in 9.98% vs 0.02%.
- **What the fix recovers:** fixed header, true seeds: 1.00 / 0.79 / 0.79 bits per slot (X2's offline sim: 1.01 / 0.80
  / 0.77; the "0.08 at 175K" of X2 §9 was this bug, that proxy has 5 restrictions pages). Interpolated over the
  70,571 slots: **about −7.4 KB, range −5 to −9 KB**, minus 0.2 KB of exe: **≈ −7 KB of S**. Low end: /1.5 for later
  stages (BITLSTM head, G1) and a C4 base that differs from the record's cost dumps the sim runs on; high end: 1.00
  bits everywhere. The real-stack analogues (0.70 / 0.78 bits per slot through the whole C4 stack) sit inside it.
- Not verified: the gain in a real full run (no full or 100M run was made), and -e mode end to end with the fix
  (only `fullstream_check`).
