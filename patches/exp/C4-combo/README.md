# C4: C3-full + T1 + G1 + P1 packaging (the next full-run candidate)

Type: **combo / submission build** (stacks two model additions and the packaging; no new mechanism except G1's
gate feature). Base: `patches/exp/C3-full`. Date: 2026-11-09.

C4 = the C3 submission build (every switch baked) + **T1** (interlanguage title copy, final stage after X2,
`patches/exp/T1-fieldcopy`) + **G1** (GLN final stage after the BITLSTM head, `patches/exp/G1-gln`) + **P1**'s size
work (`patches/exp/P1-packaging` @ db26166: p1cm-packed exe, regex-free E12, bit_head -O2, no unwind tables, FX2TFWC3
weights, rank-delta article order). Nothing is read from the environment (except the diagnostic `FX2_PRINT_VOCAB`)
or from external files: E12, X2, T1 and G1 are on exactly when a dictionary is in use (the `-c/-d` dictionary
argument in slice mode, `./.dict` in `-e` / archive9 mode). Without a dictionary (comp9's own dict/order streams)
nothing changes, so `comp_dict`/`comp_order` are byte-identical to P1's.

## What changed against the parts

- **T1/G1 baked:** T1 lr 0.004; G1 n 8, k 8, lr 0.001, lrmin 0.0003, tau 64, bscale 0.15, seed 923 (the G1 defaults).
- **G1 gate feature (new, +42 B on 10M):** z[3] = stretch(p_fin) − stretch(coded p), the BITLSTM head's own delta,
  replaces the spread (max − min) of the layer-0 mixers; p_fin = the predictor's final p (after T1, the head's input).
  Exact sim (`G1-gln/sim` `g1sim_exp` variant 6 = this feature, on the G1 dumps in build/exp/G1):

  | dump | G1 as built | with z[3] = head delta |
  |---|---:|---:|
  | enwik9_10M | −801.1 | **−842.6** |
  | enwik9_mid10M_pa (census) | −582.6 | **−627.7** |
  | enwik9_1M | −92.6 | **−101.1** |
  | enwik9_mid1M_pa | −74.7 | −71.8 |

  Better on both 10M slices (−41.5 / −45.1 B), so it's in.
- **One WRT decoder for X2 and T1** (`src/models/wrt_dec.h`): the swap table, dictionary loader and decoding state
  machine that T1 had copied from X2 are now shared code; the dictionary tables are loaded once (shared_ptr).
  Exact: `fullstream_check.cpp` built against the shared headers and against the original X2/T1 headers gives the
  same hashes of every X2 and T1 output probability over the whole real -e stream (pseudo-random p_in per bit).
  It saves 4.6 MB of RAM (full-stream check peak 92.1 vs 96.7 MB).
- **Costlog twin** (`0002-costlog-on-C4.patch`): C3-full's cost log ported; `CostLogCoded` comes after `G1Adjust`, so
  the log has the probability really coded.

## Files

- `mktree.sh TREE`: C3-full's tree (C3-full/mktree.sh) + T1/G1 headers + G1's coder hooks + P1 0001/0002/0004, commit
  "base", then `0001-C4-T1-G1-baked.patch` (T1's and G1's predictor lines merged and baked, G1's z[3], `wrt_dec.h`).
- `build.sh`: `build/exp/C4/bin/{cmix, cmix_packed, comp9, cmix_costlog, comp9_costlog, comp9.parts,
  6m-q4-fp32.tfwc3, new_article_order.rd}`. PGO trained with the dictionary under `env -i` (the build fails unless
  the training run printed T1 and G1 on), the record's guards, fails if a getenv is left.
- `run.sh packed|unpacked SLICE NOTES [--fast] [--keep]`: clean-env slice bench (FX2TFWC3 weights, --decomp-size =
  packed comp9).
- `fullstream_check.cpp`: E12 + X2 + T1 as built in -e mode over the real coded stream (`build/exp/E12/full/e9.stream`).
- `smoke.sh DIR [CODE_MIN]`: full-mode readiness of the packed submission binaries (below).
- `full_cmd.sh`: C3-full's full-mode bench wrapper with C4's binaries (compress = comp9_costlog -e, archive9's
  decompressor swapped for cmix_packed). `launch.sh`: the detached full run (build/exp/FULL-C4). **Not launched.**

## Sizes

| part | C3-full (unpacked) | P1 on C3-full | **C4** |
|---|---:|---:|---:|
| exe | 612,760 | 193,384 (513,312 raw) | **200,960** (537,888 raw; payload 197,301) |
| comp_dict | 99,889 | 99,889 | 99,889 |
| comp_order | 200,493 | 181,132 | 181,132 |
| comp_tfweights | 2,930,652 | 2,902,850 | 2,902,850 |
| header | 16 | 16 | 16 |
| **comp9** | 3,843,810 | 3,377,271 | **3,384,847** |

T1 + G1 cost +7,576 B of packed exe, i.e. about +15.2 KB of S (the exe is in comp9 and archive9).

## Results (all round trips byte-exact, clean env, ledger rows `fx2T+C4-full`, packed exe unless noted)

| slice | C3 | T1 alone | G1 alone | sum of parts | **C4** | C4 − C3 |
|---|---:|---:|---:|---:|---:|---:|
| enwik9_10M | 1,261,700 | 1,261,120 (−580) | 1,260,910 (−790) | 1,260,330 (−1,370) | **1,260,281** | **−1,419** |
| enwik9_1M | 126,244 | 126,234 (−10) | 126,151 (−93) | 126,141 (−103) | **126,132** | **−112** |
| T1 proxy `build/exp/T1/iwp_120k.xml` | 33,864 | 32,805 (−1,059) | — | | **32,731** | **−1,133** |
| census `enwik9_mid1M_pa` | 29,760 | — | 29,685 (−75) | | **29,471** | **−289** |

- **10M: 104% of the sum survives** (−1,419 vs −1,370); with the gate feature's sim gain the expectation was
  −580 − 843 = −1,423, so T1 and G1 stack additively.
- 1M packed and unpacked exe give **byte-identical archives** (126,132 B); the costlog twin's 1M archive is identical
  too (`C3-full/identity_1M.sh` with `BIN_DIR=build/exp/C4/bin`; log sum 126,090 B + 42 B header/flush).
- Census 1M: G1 alone was −75, so T1 accounts for about −210 B there (census town articles carry interlanguage links
  that repeat the English title; T1 was never benched on census). The proxy shows G1 adds −74 B on top of T1.
- Timings in the ledger are contended (C3's full run was running too).

**Full enwik9 projection (ESTIMATE):** T1 ≈ 72 KB (48–90) + G1 ≈ 84 KB (≈ 80 KB × 843/801) − 15 KB of exe ≈
**−140 KB of S vs C3 + P1**, if the slice rates hold on the warm, reordered full run.

## Full-mode readiness (2026-11-09, nothing launched)

- **`fullstream_check`** (E12 + X2 + T1, coded block header on, real 586,459,020 B -e stream): no crash, 616 s
  (1.05 µs/byte, contended); E12 slots 1,380,293, X2 time 154,985 / rpid 70,571 (= C3-full's check); **T1: 93,700
  groups found in the interlanguage block = 93,700 queued titles, 678,912 slots, 244,467 links equal to the title**.
  Peak RSS 92.1 MB for all three (C3-full's E12 + X2 check: 86 MB with the std::regex E12).
- **`smoke.sh` (packed clean comp9 -e enwik9, killed after 3 min of coding):** the stub self-extracts; the dictionary
  and the rank-delta order decode; **`.main_reordered` (999,988,851 B) and `.ready4cmix` (934,220,400 B) are
  byte-identical to the record's -e run** (so P1's order change is exact; `.new_article_order` differs only because it
  is now the rank-delta file); E12/X2/T1/G1 built on `.dict` with coded block header 1; the dict/order self-extraction
  subprocesses run without them (25 stage-1 cols, no T1/G1 lines). RSS (whole session) 4.88 GB at coder start, 5.81 GB after 3 min
  (C3's smoke: 5.07 GB at coder start, 5.87 GB after 13 min).
- **Packed archive9 layout** (cmix_packed + .dict.comp + .tfweights + empty payload + header): self-extracts and
  verifies the dictionary (99,889 → 411,996 B), builds E12/X2/T1/G1 on `./.dict`, then stops on the empty payload as
  expected (a killed -e leaves no payload; same limit as C3-full's smoke).
- **Extra RAM vs C3:** about +6 MB (T1's title queue and tables, minus the shared WRT tables) + ~0.1 MB (G1). The p1cm
  stub needs ~500 MB transiently before it execs, not during coding. C3's run is forecast at 9.75–9.8e9 B, so check
  its measured peak before launching C4.

## Launch (orchestrator's call, after the C3 run verifies)

```sh
git status   # patches/exp/C4-combo must be committed
patches/exp/C4-combo/launch.sh   # detached; writes build/exp/FULL-C4/STATUS.md
```
