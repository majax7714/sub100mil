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
| E9 | **Bug fix:** the record feeds fxcm output slot 430 (a constant 0.5) as fxcm's final prediction into layer 1 and `auxiliary_context_`. The real final is slot 428 | bug fix | ~0 | Found by A1's per-byte instrument | proposed |
| A2 | **Link-target model:** predict `[[...]]` targets by ranking earlier titles and links by prefix | architectural (specialist) | TBD | Link targets are 14.3% of bits. Never-seen targets are 10.9% of all bits at 1.61 bits/char. Can be tested offline with the per-byte tool first | proposed |

## Shared tooling
- **Per-byte cost instrument:** `patches/tools/per-byte-cost/` (README). It gives a 40 B record per coded byte: the record's cost,
  the transformer/fxcm/PPMd costs when used alone, and the match state. Map costs back to text and content classes with
  `scripts/analysis/a1_analyze.py`.

## Where the bits go (record, 10M slice; from A1)
prose 63.8% (1.11 bits per byte), link targets 14.3%, refs 6.1%, external links 4.1%, interlanguage 2.2%, tables 1.7%, templates 1.2%.
Bytes coded at ≥4 bits are 17.5% of bytes but 61% of bits. The transformer alone is only 0.9% worse than the full record.

## Tooling debt
- ~~slot.sh gives no priority to short runs~~ (fixed 2026-11-05: `--fast` lane for ≤1M runs; main lane now 2 slots).
- Agents' `until ! pgrep -f` wait loops match themselves and never exit (see scripts/README.md, "Waiting on a run").
- ~~`bench.sh` marked runs `+dirty` for uncommitted ledger rows~~ (fixed 2026-11-05).
