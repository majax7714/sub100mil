# Experiment backlog

Cheap, diverse mechanism attempts on frozen inherited weights; no dense training (see AGENTS.md, "Plan of record").
Primary base: **fx2-cmix-transformer-v1** (the record; see docs/refs/fx2-cmix-transformer.md). The neif findings are in
docs/refs/cmix-neif.md. Gains are **speculative** until measured. Every result gets a ledger row and a build log entry;
dead ends go to the attic.

## Baseline facts that constrain everything (measured 2026-11-05)

| | record (fx2-T) | neif |
|---|---|---|
| Slice result | 10M: 1,264,682 B (1.0117 bpb), raw-slice mode | 10M stream prefix: 1,918,894 B (1.535 bpb) |
| Full-run time on this box (ESTIMATE) | about 29.5 h per direction (likely low) | about 42.4 h per direction |
| Official time | 37.2 h on the 3700X → **about 7.8 h of headroom** to our 45 h target (about 48 µs per coded byte) | 48.7 h on EPYC |
| Time split | transformer 52.7%, fxcm 17.2%, 23 mixers 15.6%, PPMd 3%, other CM 4%, SSE 0.8% | transformer 58.9%, fxcm 24.2%, mixers 5.7%, LSTM 4.6% |
| Transformer bound | **compute** on its own core (IPC 3.0). L3-*capacity* sensitive: +97% time with a 12 MiB co-runner | **L3 bandwidth** (112 GB/s = the L3 ceiling) |
| Rest of the compressor | DRAM-latency bound (IPC 1.44, about 2,800 line fills per byte) | causes about 87% of DRAM misses |
| RAM headroom | **about 0.3 GB** (9.66 of 10 GB) | about 0.2 GB |

**Slice caveats:**
- Every slice is inside the transformer's training data.
- The CM models start cold on a slice.
- No article reordering runs in slice mode.

So slice gains on *transformer-side* mechanisms are roughly representative, but CM-side gains can mislead. Confirm promising results with a full enwik9 run (CPU time is free; one run takes about 30+ h per direction).

## Next session starts here (decided 2026-11-06)

**First: the C3 full enwik9 run, with the per-byte cost log on.** Don't queue more 100M checks first. The two biggest wins (E12, X2) live almost
entirely in census articles and the metadata block, which slices barely contain, so only a full run tests them. A full-scale cost dump of C3
also makes the run reusable: future specialists can be simulated offline against real warm full-file costs (E12's sims matched the real build
to 0.3%, X2's to 1 B), so we don't rerun 30 h per idea, only at milestones.

What the run must answer: actual S; peak RSS against the 10 GB limit (the record peaks at 9.66 GB, and C3 adds about 20 MB plus PPMd growth);
whether E12 + X2 deliver their projected ~300 KB (170 + 127 KB) against the warm, reordered full-run CM.

Steps:
1. (Done 2026-11-06: C2 100M 11,980,914, −0.212% vs record, round-trip OK. C2/C3 cost is about 40.5 h per direction, an estimate.)
2. Build a full-mode harness (none exists yet; `slice_cmd.sh` is slice-only): the record's `-e`/`archive9` path with the C3 binary, plus the
   per-byte cost patch (`patches/tools/per-byte-cost/`) stacked on C3.
   - **The runtime switches must be baked in.** The submission can't depend on env vars or external files. The E2/E3/E12/X2 env settings
     become compile-time defaults, and E12/X2 read the embedded dictionary (`.dict` in the cwd in full mode; see patches/exp/E12-arith/README.md
     "Known limits").
   - Rebuild comp9 with UPX and measure the decompressor size for S.
3. Run compress then decompress in one main-lane slot with `BENCH_TMP` on disk (PPMd's `ppm.temp` is about 14 GB), using
   `/usr/bin/time -v`, and `cmp` against `data/raw/enwik9`. The cost dump is about 40 B per coded byte, about 23 GB. Expect 60+ h of
   contended wall time; timings aren't usable for the time budget (use the perf-based estimates).
4. While it runs, use the other main slot and the fast lane for **batch 4**: several cheap, diverse, off-plan mechanisms, at least two
   non-transformer. Skip 100M checks unless the change is transformer-side. Pending small items: X2's 30K warm-record proxy (doc §9), and a
   size-optimized X2/E12 build.

## Candidates

| ID | Mechanism | Type | Cost | Why we think it might work | Status |
|---|---|---|---|---|---|
| E1 | Transformer **confidence** (entropy, margin of its 205-way distribution) as a context for the mixers and final SSE | model addition | <1 µs per byte, about 0 RAM | Currently collapsed to a single bit probability. Confidence-gated mixing is cheap and standard in CM | **small gain (keep):** gate3, 10M −410 B (−0.032%). About 0.55 h on the reference machine, ≤26 MB RAM (patches/exp/E1-confidence) |
| E2 | **Online correction head** on the transformer's logits or 192-d hidden state (fast weights) | model addition | a few µs per byte, about 160 KB | ex-girlfriend did this on neif and measured −140,692 B (see LANDSCAPE.md) | **best so far (keep):** online logit head on the 192-d hidden state, 10M −2,123 B (−0.168%). About 1.0 h on the reference machine, about 0.3 MB (patches/exp/E2-correction-head) |
| E3 | Port neif's **BITLSTM** (online-only first) | model addition | about 1% of time | neif: about −0.3 MB (estimate) for 1.1% of time | **small gain:** 10M −326 B (−0.026%), still growing with length. About 1.4 h at full scale. Next: CPU replay-training of the head, or a 100M check (patches/exp/E3-bitlstm) |
| E4 | **Prune or merge the 23 mixers** to buy time | speed | frees up to 16% of time | lexth11c: −10% time for +0.024% size | proposed |
| E5 | **Software prefetch** for the CM side's DRAM misses | speed | ~0 | The CM side is DRAM-latency bound. In neif, `Mixer1::p1` causes 26% of misses | proposed |
| E6 | Shrink the transformer's **L3 footprint** (int4 kept packed in cache) | speed / robustness | engineering | Protects against the 12 MB-L3 Intel test box (neif took 82.9 h there) | proposed |
| E7 | **Packaging:** UPX/stub, container format, dead-neuron removal | size | ~0 runtime | about −0.3 MB of S estimated across neif's packaging items. Our comp9 is +173 KB vs official from UPX alone | proposed |
| E8 | **clang build** (needs `clang17`) | speed | ~0 | The agent estimates 8–15% faster kernels | proposed |
| A1 | **Architectural bet:** a stronger long-range match/retrieval model over the file itself (templates, repeated phrases), fed to the mixers | architectural | TBD | Deterministic, CPU-only, no training. enwik9 is highly repetitive | **NO-GO** (attic/2026-11-05-A1-retrieval). Realistic gain about 10–20 KB |
| E9 | **Bug fix:** the record feeds fxcm output slot 430 (a constant 0.5) as fxcm's final prediction into layer 1 and `auxiliary_context_`. The real final is slot 428 | bug fix | ~0 | Found by A1's per-byte instrument | **neutral:** 10M +6 B. fxcm's final already reached the layer-0 mixers, so nothing missed it. Kept in C1 at no cost (patches/exp/E9-fxcm-final) |
| C1 | **Combination** E1 gate3 + E2 logit head + E3 agg16 + E9 | combination | about 15 µs per byte, about 3.1 h on the reference machine, +13 MiB, about +15–20 KB of S (estimate) | Check whether the small wins add up | **current best:** 10M 1,262,015 B, −2,667 (−0.211%), 93% of the sum. All overlap is E1↔E2; E3 is fully additive (patches/exp/C1-combo) |
| A2 | **Link-target model:** predict `[[...]]` targets by ranking earlier titles and links by prefix | architectural (specialist) | TBD | Link targets are 14.3% of bits. Never-seen targets are 10.9% of all bits at 1.61 bits/char. Can be tested offline with the per-byte tool first | **NO-GO** (attic/2026-11-05-A2-link-targets). About +17 KB projected (0 to 40 KB); the record already ranks targets better. Only same-article links help |
| E10 | **Dynamic evaluation:** one SGD step per token on per-channel gains of all 24 branch outputs plus per-neuron relu² gains (the quantization scales), truncated gradient with an exact int16 MLP backward, decay toward the frozen weights | model addition (transformer-side, online learning) | bk12q4d about 35 µs per token, **about 5.7 h on the reference machine (over budget)**; bk6q4d about 3.3 h; about 0.25 MB | The record adapts nothing on the transformer side online (step 0) | **works with decay:** bk12q4d 10M 1,261,985 B, −2,697 (−0.213%). Without decay it drifts (+66 B on 10M). Overlaps strongly with E2. Next: bk6q4d and bk12q4d+E2 on 10M, decay tuning on 100M, cheaper backward (patches/exp/E10-dyneval) |
| E11 | **In-article cache model:** a recency-weighted distribution over WRT word codes and phrases already used in the current article, fed to the mixers | model addition (non-transformer) | small | From A2: the record still pays 6.5 bits for a link target already linked in the same article. Would also cover prose (64% of bits). Test offline with A2's `BitSim` first | **NO-GO for now** (attic/2026-11-05-E11-in-article-cache): 10M +702 B offline, about 60 KB projected (45–90 KB). Gain is on in-article repeats of rare tokens. Revisit by re-simulating on top of an E10/C1 cost dump |
| E12 | **Arithmetic-consistency specialist:** numbers that are exact functions of earlier ones (census k/P and k/H percentage lattices, last items summing to 100, densities, km²↔mi², sex ratio, unit conversions anywhere), as a causal final stage after SSE | model addition (non-transformer specialist) | about 0.95 µs per byte (about 0.2 h, an estimate), <1 MB, about +3 KB of S | Neither CM nor the transformer can divide; the record pays 58.3 KB (17%) on derivable slots in mid10M | **GO:** census 10M slice (mid10M_pa) 337,982 → 325,041 (−3.83%), 10M −123 B, census 1M −4.33%, all round-trip OK; sims match the real build to 0.3%; **full enwik9 ESTIMATE about 170 KB (140–190 KB)**. Next: full-mode block headers, lr 0.004, 100M, fold into C1 (patches/exp/E12-arith) |
| A3 | **Encoder-side selection:** fork a cheap component's state per block, try K variants, commit the best, send the choice; the decoder replays only the winner (first target: E2 head LR) | architectural (asymmetric compute) | encoder K× head cost (K=4 about 3.1 h), decoder about 1× | Everything in the record is symmetric; the encoder could search | **NO-GO** (attic/2026-11-06-A3-encoder-select): 10M oracle −149 B head loss (≈ −16 to −83 B coded), greedy fork-and-commit loses. By-product: E2 LR ×0.5 in C1, 10M 1,261,970 (−45 B) |
| X1 | **Cross-domain synthesis:** 25 mechanisms from physics, biology and chemistry mapped to bytes; tested Potts/DCA token couplings within an article and coarse-grained state as a final context | model addition (non-transformer), offline | about 6–7 µs per byte, 0.6–1.2 GB (over the RAM headroom) | A deliberate long shot from outside CS | **NO-GO** (attic/2026-11-06-X1-cross-domain): couplings +0.03% on 10M over a warm control, about 35 KB (10–60 KB) projected; coarse state ≤ 28 B. The survey table is in docs/design/X1-cross-domain.md |
| X2 | **Cross-domain round 2** (fields X1 skipped + domain laws as computation). Hit: **metadata time law**: revision ids are time-ordered, so each page's timestamp in phda9's metadata block is predictable from its revision id (stratigraphy analogy); plus a redirect page-id law | model addition (non-transformer specialist) | seconds of CPU, <20 MB, about 2 KB of S | E12's lesson: the record is blind to computation, wherever the structure lives | **GO, real build round-trips** (patches/exp/X2-timelaw, final stage after E12): proxies −534/−554/−1,046 B over E12 per MB, equal to the sim within 1 B. 154,990 slots at 10.8–15.7 bits; reordered-block proxies +1.1 to 2.3 KB per MB; **full enwik9 ESTIMATE about 127 KB (85–135 KB)**. Text laws (ISBN, chronology, …) NO-GO, about 14 KB (attic/2026-11-06-X2-text-laws). Next: fold into C2, 30K warm proxy |
| C2 | **Combination** C1 + E2 logit LR ×0.5 + E12 (lr 0.004) | combination | C1's cost + about 0.2 h | Fold the batch-3 wins | **10M 1,261,828 (−2,854 vs record)**; census 10M slice 323,802 (−4.20%); **100M 11,980,914 (−25,409, −0.212% vs record; −7,435 vs E2 alone)**; about 40.5 h per direction (ESTIMATE), +20 MiB (patches/exp/C2-combo) |
| C3 | **Combination** C2 + X2 metadata time law | combination | C2's + seconds | Fold X2 | **current best: 10M 1,261,700 (−2,982, −0.236% vs record)**; 1M 126,244; X2 proxy r130K: C2 25,219 → C3 24,175 (−1,044, same as on the record alone). **Next: full enwik9 run** (see top) (patches/exp/C3-combo) |

## Shared tooling
- **Per-byte cost instrument:** `patches/tools/per-byte-cost/` (README). It gives a 40 B record per coded byte: the record's cost,
  the transformer/fxcm/PPMd costs when used alone, and the match state. Map costs back to text and content classes with
  `scripts/analysis/a1_analyze.py`.

## Where the bits go (record, 10M slice; from A1)
prose 63.8% (1.11 bits per byte), link targets 14.3%, refs 6.1%, external links 4.1%, interlanguage 2.2%, tables 1.7%, templates 1.2%.
Bytes coded at ≥4 bits are 17.5% of bytes but 61% of bits. The transformer alone is only 0.9% worse than the full record.

## Tooling debt
- **Warm control for CM-side slice gains** (from X1): cold slices flatter vocabulary mechanisms by about 3% on 10M. Check CM-side ideas against
  `scripts/analysis/x1_sim.cpp` with `X1_WARM=...` before trusting them.
- **Before any full enwik9 run:** set `BENCH_TMP` to a disk path. PPMD's sparse `ppm.temp` (about 14 GB at full scale) otherwise lands in the
  RAM-backed `/tmp` tmpfs. At 100M it held fine; peak RSS was 6,558 MiB, up from 5,797 at 10M, mostly PPMD growth.
- ~~slot.sh gives no priority to short runs~~ (fixed 2026-11-05: `--fast` lane for ≤1M runs; main lane now 2 slots).
- Agents' `until ! pgrep -f` wait loops match themselves and never exit (see scripts/README.md, "Waiting on a run").
- ~~`bench.sh` marked runs `+dirty` for uncommitted ledger rows~~ (fixed 2026-11-05).
