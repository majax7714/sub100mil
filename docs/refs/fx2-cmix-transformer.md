# fx2-cmix-transformer-v1: build and characterization

The current official Hutter Prize record (L = 100,424,672, Vladimir Ivanov, awarded 30 Sep 2026) is our
primary baseline. This note covers what we found building and measuring it on the dev box (Ryzen 5 8600G,
Zen 4, Fedora 43, g++ 15.3.1). Its transformer weights are frozen and inherited; nothing here retrains them.

- Source: `third_party/fx2-cmix-transformer-v1` at `83f2603f3f7751da5f429fd32669807eb8510494` (GPL-3.0).
- Our patches, build script and tools: `patches/fx2-cmix-transformer/`.
- Outputs: `build/refs/fx2-cmix-transformer-v1/` (gitignored).
- Reproduce with `patches/fx2-cmix-transformer/build.sh`, which takes about 6.5 min, including about 4.5 min of
  CM-compressing the dictionary and article order into comp9.

Labels: **[M]** measured here; **[ESTIMATE]** derived; **[UP]** taken from the upstream repo's own docs
(`writeup.md`, `cpp_infer/REPORT.md`, `cpp_infer/MACHINE.md`, all measured on Zen 2).

## 1. Audit (before running anything)

We read `build_and_construct_comp.sh`, `makefile`, `setup.sh` and `make_training_data.sh`, plus everything they
call: `src/runner.cpp`, `src/readalike_prepr/*` and `src/models/ppmd.cpp`.

| Item | Finding | Action |
|---|---|---|
| `setup.sh` | `sudo apt-get install upx-ucl make`, then `wget https://apt.llvm.org/llvm.sh; sudo ./llvm.sh 17`. That is remote-script-as-root, equivalent to curl\|sh. Ubuntu-only. | **Not run.** |
| `makefile` target `data/enwik9` | `wget http://mattmahoney.net/dc/enwik9.zip`. Only `make_training_data.sh` reaches it, not the compressor build. | Not run (we have `data/raw/enwik9`). |
| `build_and_construct_comp.sh` | No network and no sudo. Writes only inside the repo (`pgo_data/`, `run/`, objects). Needs `clang++-17`, `llvm-profdata-17` and `upx-ucl`. Includes two good guards: an objdump check for AVX-512/AVX-VNNI/EVEX and RCPPS/RSQRTPS, and an md5 check of the dictionary. | Not run as-is (toolchain missing). Reimplemented in `patches/fx2-cmix-transformer/build.sh`. |
| `pyproject.toml` / `uv.lock` | Python 3.11, torch 2.12, flash-linear-attention, flash-attn as a prebuilt wheel from a GitHub release URL, and wandb (phones home during training). | Not installed. Only needed to retrain. |
| Runtime (`cmix -e`, `archive9`) | Self-extractors call `system("./cmix -d ...")` or `system("./archive9 -d ...")`, so they must be run as `./name` from their own directory. They write about 20 dot-files into the cwd (`.dict`, `.main*`, `.ready4cmix`, ...). PPMD (order 25, **14,000 MiB**) is `mmap`ed to a sparse `./ppm.temp` (14.7 GB apparent size) and re-mapped every 20,000 bytes. `-c` mode calls `remove(".dict")` in the cwd. No network. Nothing is written outside the cwd except `tmpfile()`s in `/tmp` (phda9). | Every run happens in a scratch work dir. |
| Repo contents | Prebuilt x86 binaries are committed under `cpp_infer/bench/` (`gemv`, `membw`, `l3_thrasher`, ...). `models/` holds 200 MB of `.tch` checkpoints. Only `models/6m-q4-fp32.tfwc2` (2,930,652 B) is used at runtime. | Binaries not executed and not copied. We wrote our own `membw`. |

Missing to reproduce upstream's exact toolchain (no sudo was used; `dnf repoquery` shows the packages exist on
Fedora 43): **`sudo dnf install clang17 llvm17 upx`**. That gives clang 17.0.6, llvm-profdata 17 and upx 5.x;
upstream used `upx-ucl` from Ubuntu. perf was installed by the user during this session.

## 2. Build

`patches/fx2-cmix-transformer/build.sh` copies the pinned tree to `build/refs/fx2-cmix-transformer-v1/tree`,
applies the patches with `git apply` (`patch(1)` is not installed), builds, and runs upstream's portability
guards on the result. Upstream's makefile hardcodes `CC = clang++-17` and clang-only flags, so **we override
them** (patch 0001):

| Upstream (clang 17) | Ours (g++ 15.3.1) | Why |
|---|---|---|
| `-ffp-model=fast` (CM objects) | `-ffast-math -fno-finite-math-only -ffp-contract=fast` | gcc spelling of the fast FP model. See the next row for `-fno-finite-math-only`. |
| `-mrecip=none` | kept, **but it is not enough with gcc** | gcc 15 still emits `vrcpps`/`vrsqrtps` under `-ffast-math -mrecip=none` (in the `Sigmoid` and `fxcmv1::Predictor` table constructors). Those instructions give vendor-specific bits, the Intel-vs-AMD desync upstream fixed in July 2026. Only `-fno-finite-math-only` (or `-ftrapping-math`) stops it. It also undefines `__FAST_MATH__`, so glibc's libmvec SIMD `expf`/`tanhf` are not used. The release binary has 0 RCPPS/RSQRTPS and no libmvec symbols, and build.sh fails if either appears. |
| transformer objects: `-O3`, no fast-math, no PGO | same | upstream's choice. |
| `-march=x86-64-v3 -mtune=generic` | same (upstream's default, never `NATIVE=1`) | AVX2 baseline. The AVX-512/VNNI/EVEX objdump guard passes. |
| PGO: `llvm-profdata merge`, `-fprofile-use -flto` | gcc `-fprofile-generate/-fprofile-use -fprofile-partial-training -flto=auto` | Same training run as upstream: `./cmix -c prof_input/input` (50 KB, CM only, no transformer). `partial-training` keeps unprofiled hot code, e.g. the transformer glue, at -O3 rather than -Os. |
| `upx-ucl -9` on the binary | **skipped** (not installed) | See the size table. |
| missing `#include <cstdint>` in `fxcmv1.cpp` | added | clang pulled it in transitively. |
| `make -j` with `prof_gen: clean cmix` | build serially | upstream race: `clean` deletes `tf_*.o` while they build. |

Variants (all `-march=x86-64-v3`): `cmix` (release, PGO+LTO), `cmix_syms` (same profile with symbols kept;
`.text` checked byte-identical to `cmix`), `cmix_noPGO`, `cmix_prof` (`-DFX2_PROF`, see §5), and `comp9` (the
self-extracting compressor, i.e. upstream's `run/cmix`).

### comp9 size [M]

| Part | Ours (g++, no UPX) | Official |
|---|---|---|
| executable | 369,792 (stripped, unpacked) | ≈196,600 [ESTIMATE: 497,806 − our dict+order] |
| `comp_dict` (english.dic, CM-compressed by the binary itself) | 100,114 | |
| `comp_order` (article order, CM-compressed) | 201,077 | |
| `comp_tfweights` (FX2TFWC2, used as-is) | 2,930,652 | 2,930,652 |
| header | 16 | 16 |
| **comp9 total** | **3,601,651** | **3,428,474** [LTCB] |

The +173,177 B difference is almost entirely UPX. Official exe + dict + order = 3,428,474 − 2,930,652 − 16 = 497,806. If their
dict/order streams are about the same size as ours (same CM; only FP rounding differs), the official UPX'd
executable is about 196.6 KB, which is 53% of our 369.8 KB. That is consistent with generic packers on our
binary: gzip -9 gives 182,273, zstd -19 167,894, xz -9e 157,020. UPX itself was not available to confirm it.
PGO and LTO make the binary *smaller*: `cmix_noPGO` is 461,088 B. The build is deterministic: two
from-scratch builds produced identical sizes for all three self-compressed parts.

### Bit-exactness: what it depends on

- **Compiler and flags: yes.** CM objects use fast-math (reassociation, FMA contraction). The transformer uses
  plain `-O3`, but contraction defaults differ: clang's `-ffp-contract=on` fuses only within an expression,
  while g++ defaults to `fast` even under `-std=c++17` and fuses across statements (checked: `a*b+c` becomes
  `vfmadd` with both `-std=c++17` and `gnu++17`). So **a g++ archive
  and a clang archive are not interchangeable**, and nothing promises gcc 14 vs 15 agree. Each binary is
  self-consistent. The archive embeds the same binary that compressed (`archive9` = decompressor exe + payload),
  so encoder/decoder symmetry holds whenever both run *the same binary*. Our build passes upstream's
  self-test: `comp9 -e` re-decodes its own CM-compressed dictionary and checks the FNV-1a hash.
- **PGO/LTO and instrumentation: apparently not, with this gcc.** The PGO+LTO release, `cmix_noPGO` and
  `cmix_prof` produced the same 1M archive size (126,473 B; sizes compared, not bytes). Treat that as
  evidence, not a guarantee: every archive must still be decoded by the binary that made it.
- **CPU vendor: no, provided the binary has no RCPPS/RSQRTPS** (upstream's July 2026 bug). This is exactly what
  `-mrecip=none` alone no longer guarantees under gcc 15 (above).
- **glibc version: possibly (UNVERIFIED risk, applies to the official binary too).** The binary links libm
  dynamically (`exp`, `expf`, `log`, `logf`, `powf`, `tanhf`, `sincosf`), e.g. in the Sigmoid/stretch tables
  and in the KDA `a_neg = -exp(.)` constants built at load time. glibc 2.41+ replaced several float functions
  with correctly-rounded CORE-MATH versions, so compressing and decompressing on machines with different glibc
  could in principle desync. Static linking (`-static`) would remove the risk at some cost in size.
- AVX2-only is enforced at build time. The upstream objdump guard is re-run in build.sh.

## 3. Slice compatibility

**The submitted mode (`cmix -e enwik9`, and `archive9` with no arguments) is enwik9-only.** Hard dependencies:

- `misc.h` splits by hardcoded **line numbers**: intro = lines 0–28, main = lines 29–13,146,931, coda = the
  rest. The decompressor uses a second set of constants (13,146,906 / 13,146,935).
- `article_reorder.h`: `NUM_OF_ARTICLES 243425`, plus a 1,094,862-byte article order file
  (`src/readalike_prepr/data/new_article_order`) indexed by enwik9 article number.
- The transformer's 205-token vocabulary is the byte set of the *full* preprocessed enwik9 stream. Token id =
  rank of the byte in that set. `Predictor` fails unless the input's vocab has exactly 205 bytes, and on a
  prefix the set is smaller, so the ids would also shift.
- Article boundaries for the transformer come from the 15-byte WRT encoding of `"  <page>\n    <title>"`.
- The dictionary (`english.dic`, md5 `b3a9cf9f...`) is a fixed asset, hash-checked by the decompressor.

There is an upstream testing mode, `cmix -c|-d english.dic in out --transformer W`, that runs the full model
on any input. On its own it fails the 205-vocab check, and it skips the enwik9 phda9 transform the transformer
was trained on. **Our minimal change:**

1. **Patch 0002:** when the transformer is enabled, force the enwik9 vocab mask (`kEnwik9Vocab`, read once from
   `FX2_PRINT_VOCAB=1 comp9 -e enwik9` on this build: 205 bytes) and fail if the input has a byte outside it.
   The mask goes into the archive header, so the decoder is unchanged. On enwik9 itself this is a no-op.
2. **`slicetool` (patch 0003):** the -e preprocessing for a prefix. intro = first 29 lines; main = up to the
   last complete `  </page>` line; coda = the rest. Then the phda9 WIT transform on main, and main_enc + intro
   + coda in -e's order, plus a 40-byte size trailer. `slicetool post` inverts it. It round-trips 1M, 10M and
   full enwik9 byte-exactly. On full enwik9 it reproduces -e's split exactly (main 999,988,851 B, coda
   9,745 B).

Wrapper: `patches/fx2-cmix-transformer/slice_cmd.sh comp|decomp in out` (used through `scripts/bench.sh`).

**What is representative:** the same binary, the same models and mixer topology, the same WRT dictionary and
pretraining, the same phda9 transform, the same transformer inputs (PPMD prior plus the token), the same
per-article context resets, and the same frozen weights.

**What is not:**

- **No article reordering.** Slices keep enwik9's id order. On full enwik9, -e's reorder (which groups similar
  articles) is part of the CM's gain. It doesn't matter to the transformer (each article is a fresh context).
- **Cold start.** A 1M/10M slice is mostly the warm-up of the CM: hash tables are near-empty and PPMD is tiny.
  So the CM's share of bits is worse and its per-byte time is lower (fewer cache misses) than late in the full
  run. The transformer is warm from the first article, so it is relatively stronger on slices.
- **Memorized data.** The transformer was trained on all of enwik9 (320 + 256 + 16 = 592 epochs [UP]), so any slice is in its
  training set. That is equally true of the real run.
- Startup costs (model construction, dictionary pretraining) are amortized over far fewer bytes.
- Stream size: enwik9 → 586,459,020 coded bytes through -e [M] (WRT stream; 0.5865 bytes per input byte).
  The identity-order slicetool stream is 585,969,900 on the full file.

## 4. Results [M]

Setup: every run through `scripts/bench.sh` under the shared bench lock, pinned with `taskset -c 2`, release
`cmix` (PGO+LTO), and slice mode (§3). Round-trip is verified by `cmp`. `S` uses our un-UPX'd comp9
(3,601,651); the official comp9 is 3,428,474. Ledger rows have method `fx2-cmix-transformer-v1 g++15 PGO
(slice mode)`.

| Slice | Output bytes | bpb | Comp s | Decomp s | Peak RSS | Coded stream bytes | µs per coded byte (comp, minus about 11.5 s of startup) | Round-trip |
|---|---|---|---|---|---|---|---|---|
| enwik9_1M | 126,473 | 1.0118 | 112.06 | 114.99 | 5,673 MiB | 552,288 | 182.1 | OK |
| enwik9_10M | 1,264,682 | 1.0117 | 1053.15 | 1051.56 | 5,798 MiB | 5,764,509 | 180.7 | OK |

The first 1M attempt failed in 0.04 s, before the 5-byte raw-header fix (§3, patch 0002). That row is in the
ledger as a failure.

For reference: xz -9e on enwik9_1M gives 290,732 B (2.33 bpb). On full enwik9 the official archive is 96,996,198,
which is 0.776 bpb over 10^9 bytes. Slices from the start of enwik9 code worse per byte (CM cold start, see §3).

**Full-enwik9 time on this box (ESTIMATE, not measured):**

- The coded stream is 586,459,020 bytes [M, -e].
- Release comp time per coded byte, after removing about 11.5 s of startup (pretraining, construction,
  slicetool): **182.1 µs on 1M and 180.7 µs on 10M**. There is no growth yet between 1M and 10M. The
  instrumented coding loop on 1M gives 186.6.
- Fixed costs: self-extracting the dictionary and order file 258 s, -e preprocessing about 60 s, dictionary
  pretraining 10.5 s, model construction 0.9 s.
- 586.46M × 180.7 µs + 330 s ≈ **29.5 h ≈ 106,300 ns per enwik9 byte on this box (ESTIMATE)**, for comp and
  decomp alike (they measure within 0.3% of each other on 10M).
- This is likely an *underestimate*: the CM's per-byte cost grows as its GB-scale tables fill (more DRAM misses)
  and PPMD grows toward its 14 GB.
- Upstream's own figures on the AMD 3700X are 133,766 / 131,561 ns/B, i.e. 37.2 / 36.5 h for comp / decomp.
  That works out to **228 / 224 µs per coded byte** [LTCB; per-coded-byte conversion ours]. Our estimate is
  1.26× faster than that.
- The 8600G is about 1.5× faster single-core than the 3700X (Geekbench 5; **UNVERIFIED**, no score was
  looked up for this box). Even so, our
  slice-based per-byte cost is only about 1.26× better than the official per-byte figure. Plausible reasons:
  - The official full-run figure includes the CM slowdown as tables fill.
  - Upstream says gcc compiles their GEMV kernels 8–15% slower than clang [UP MACHINE.md].
  - DRAM-latency-bound CM time does not scale with Geekbench.

## 5. Where the time goes [M]

Two independent methods on the 1M slice agree:

1. `cmix_prof`: `-DFX2_PROF` rdtsc scopes around each model family in `Predictor`, plus upstream's own
   per-part transformer profiler. It is not PGO'd; total time is within 1% of `cmix_noPGO` and 2% of the
   PGO release.
2. `perf record` on `cmix_syms` (the PGO release with symbols), self time grouped by symbol namespace.

| Component | rdtsc, % of coding loop | µs per coded byte | perf, % of process samples |
|---|---|---|---|
| **Transformer step** (incl. f16 glue) | **52.7%** | **98.4 (per token)** | **49.5%** (`fx2::opt::*`) |
| fxcm (fx2-cmix's paq8-style model, ≈430 inputs, own mixers/APMs) | 17.2% | 32.0 | 24.3% (`fxcmv1::*`) |
| cmix mixers (23 layer-0 + 1 final, 461 inputs each) | 15.6% | 29.1 | 5.5% + part of 14.0% |
| other CM models (bracket, direct, match, indirect, run map) | 4.0% | 7.4 | 0.9% + part of 14.0% |
| PPMD (order 25, 14 GB) | 3.0% | 5.6 | 1.9% |
| SSE | 0.8% | 1.5 | <0.1% |
| byte mixer, loss accounting, context manager | 0.6% | 1.1 | 1.5% |
| arithmetic coder, loop, timer residue | 6.2% | 11.5 | 0.1% |
| *coding loop total* | *103.07 s* | *186.6* | |
| WRT preprocessing / model construction / dictionary pretraining | 0.03 / 0.9 / 10.5 s | | |

With LTO, perf charges the CM models' `Predict()` lookups and the 23 mixers' `Mix()` to an inlined
`Predictor::Predict()` (14.0%). That is why perf shows less for "mixers" and "other CM" and more for fxcm.
Preprocessing (slicetool/phda9 plus WRT) is negligible: about 60 s for all of enwik9.

Inside the transformer (upstream's per-part profiler; TSC ticks at 4.35 GHz; ≈411.6K ticks or 94.6 µs per
token in sections):

| Part | Share | Biggest sites |
|---|---|---|
| int4×int8 matmuls (86 calls per token) | 51.9% | MLP up + relu² 20.7%, KDA q/k/v projections 11.1%, MLP down (sparse) 6.6% |
| sliding-window attention (3 layers, W=1024) | 26.3% | QK + softmax + PV 26.2% |
| KDA linear attention (9 layers) | 18.5% | |
| norms, quantization, rope, head | 3.3% | |

Upstream measured the same shape on Zen 2: 49.7 / 29.3 / 17.4 / 3.6% at 516K core cycles per token
[UP REPORT.md]. Ours is about 449K core cycles per token at an average 4.56 GHz (98.4 µs).

## 6. Bandwidth or compute? Evidence

Microbenchmark: `patches/fx2-cmix-transformer/tools/membw.cpp`, AVX2, one core, pinned, best of 5. It
measures single-core sequential read bandwidth on this box:

| Working set | ≤32 KiB (L1) | 64–384 KiB (L2) | 1 MiB | 2–12 MiB (L3) | 16 MiB | 32 MiB | ≥128 MiB (DRAM) |
|---|---|---|---|---|---|---|---|
| GB/s | 270–290 | 151–153 | 128 | **117–124** | 102 | 88 | 60–65 |

A `vpmaddubsw` consumer reads at the same rate, so the int8-dot ALU doesn't cap streaming at these sizes.

**Weight bytes vs cache:**

- The model has 6M int4 weights.
  - Packed, they would be 2.9 MB, which is 2.9× the 1 MiB L2.
  - Upstream deliberately keeps them *unpacked as int8* at inference: 4.21 MB of dense arenas plus 1.20 MB of
    sparse int4 column arenas, 5.41 MB in total [UP].
- The per-token hot set is about 7.2 MB, including KV rings 1.1 MB and KDA state 0.4 MB [UP].
  - That is 7× L2 and 45% of the 16 MiB L3.
- So every token streams the weights from L3. Nothing about this model can be L2-resident.

**perf stat, `--transformer-only`** (PPMD + transformer, 552K tokens, 59.25 s):

| Counter | Value |
|---|---|
| IPC | **3.01** (815G instructions / 270G cycles, 4.56 GHz) |
| topdown | retiring 50.3%, **backend core-bound 26.4%, backend memory-bound 11.4%**, frontend 9.4% |
| L2 fills | 97.3K lines (**6.2 MB**) per token, 78% of them by the L2 hardware prefetcher from L3 |
| L3→L2 rate | **≈61 GB/s averaged over the step**, i.e. about 50% of the measured 120 GB/s single-core L3 ceiling |
| DRAM fills | about 2.3 KB per token, i.e. L3-resident |

During the matmul phase (52% of the step, about 51 µs, streaming about 5 MB) the instantaneous rate is
**about 90–105 GB/s**, i.e. 75–90% of the L3 ceiling. This matches upstream's Zen 2 finding: matmuls at 75.6%
of their bandwidth floor, attention and KDA compute-bound.

**Contention test** (`tools/contention.sh`). The transformer-only run is pinned to CPU 2 while a `membw` streamer
runs on CPU 4 (a different physical core sharing the L3). Outputs are bit-identical in every run.

| Co-runner working set | none | 0.5 MiB (L2 only) | 4 MiB | 8 MiB | 12 MiB | 64 MiB (DRAM) |
|---|---|---|---|---|---|---|
| Time (s) | 59.25 / 60.36 | 61.89 | 64.02 | 71.64 | **116.58** | 77.92 |
| Slowdown | — | +4.5% (clock/power only) | +8% | +21% | **+97%** | +31% |

**Verdict.** The transformer is **L3-capacity-sensitive and only partially bandwidth-bound; in isolation it is
mostly core-bound**:

- Over the whole step it is core-bound: IPC 3.0, only 11% memory-bound stall slots, and it uses about half
  the L3 bandwidth.
  - Attention and KDA, 45% of the step, are pure compute: exp, softmax and PV in fp32, and the KDA recurrence.
  - The matmul phase runs near the L3 bandwidth roof, at 75–90%.
- Its real exposure is **L3 capacity**.
  - A co-tenant that takes about 4 MiB costs about 3–4% (beyond the 4.5% clock effect).
  - At 8 MiB the cost is +16%. At 12 MiB, where 7.2 + 12 > 16 MiB, the weights spill and the step time
    **doubles**.
- The CM, despite its GB-scale random traffic, does *not* hurt the transformer.
  - It makes about 2,800 DRAM line fills per coded byte, but that is only about 0.9 GB/s, latency-bound and
    scattered.
  - Step time is 98.4 µs inside the full model vs about 102 µs in `--transformer-only`.
  - The transformer's hot lines win the L3 replacement.
- The non-transformer half of the run is the opposite case: **DRAM-latency-bound**.
  - The non-transformer cycles run at IPC ≈ 1.44, with 65% of cycles having a load outstanding (full run
    minus transformer-only).
  - So the CM's time scales with memory latency, not with core speed or bandwidth.

## 7. Where CPU-side mechanism changes could fit

**Free time budget.**

- Official timing is 37.2 h (comp) on the 3700X. Our ≤45 h target leaves **about 7.8 h, or about 21%**. The 53.4 h
  hard limit leaves about 16 h.
- Per coded byte: 45 h allows **276 µs**; the official run uses 228. That leaves about 48 µs per coded byte.
  On the dev box today, the transformer step is about 98 µs per coded byte and the 23 mixers about 29 µs.
- RAM: the official run peaks at 9.55–9.66 GB (with PPMD mmapped to disk) against a 10 GB limit, so almost no
  RAM headroom. A new model must be small or take memory from PPMD/fxcm.
- L3: about 8–9 MiB is free beside the transformer's hot set before step time degrades. A new *cache-hot*
  structure must stay well under 4 MiB, or the transformer pays for it.

**Unused or under-used inputs.**

- **The transformer's 205-way distribution is collapsed to one bit probability.** Only two things reach the
  CM: `byte_mixer_output` as one input to the 23 mixers, and `lstmpr`/`lstmex` (bit probability and arg-max
  byte) as 2 inputs and 1 mixer context in fxcm.
  - Its *confidence* is never a mixer-selection or SSE context: entropy, top-1 margin, and whether the
    arg-max still lies in the current bit range.
  - Computing it is about 205 ops per byte, i.e. free.
- **The transformer's internals are discarded.** `last_logits()` (pre-softmax logits) and the 192-d final hidden
  state are ignored, because only softmax probabilities go downstream.
  - A small online-trained head on that 192-d state could capture article-specific statistics the frozen
    weights cannot (dynamic-evaluation / fast-weight style; cf. ex-girlfriend in LANDSCAPE).
    - Over the 205-way vocab it is 192 × 205 ≈ 39K MAC per byte, i.e. a few µs.
    - Binary per-bit heads would be cheaper still.
- **Per-article context resets.** Each article (and each 128K-token piece) starts a fresh transformer context,
  and the first token of each piece falls back to PPMD.
  - Nothing carries over between articles: titles, the redirect structure, or the article order chosen by the
    reorder step.
  - neif's prompt injection is the proven use of this; `begin_article(rope_offset)` already exists.
- **The single final SSE** sees only `p`. Shelwien's SSE chain is 0.8% of the time. An APM/SSE stage keyed by
  transformer confidence or order-2 context is the classic cheap win.

**Cheap places to add models.**

1. **A final-stage mixer/SSE context from transformer confidence.** It costs nothing in memory and under 1 µs
   per byte, and fits after `mixer_1_` in `Predictor::Predict()`.
2. **An online correction head on the transformer's hidden state or logits.** It costs about 2–5 µs per byte,
   and the matrix is about 160 KB in fp32, comfortably cache-resident. It would plug in at
   `Predictor::TransformerByteUpdate()`, between `step()` and `SetProbs()`, where the transformer's
   distribution is replaced by a mixed or adapted one.
3. **Cut mixer cost to pay for new models.**
   - The 23 layer-0 mixers over 461 inputs cost 29 µs per coded byte, about 16% of the loop.
   - lexth11c showed pruning 23 → 12 mixers costs +0.024% size for −10% time [LANDSCAPE].
   - That frees about 15 µs per byte in this codebase too. ESTIMATE: by analogy, unmeasured here.
4. **Faster transformer kernels.**
   - Our gcc build uses upstream's clang-tuned kernels, and upstream says gcc is 8–15% slower on GEMV.
   - A clang 17 build is one `dnf install` away and is a free speed check.
   - Fusing the attention exp/PV passes or the KDA sweep attacks the compute-bound 45%.
   - Packed int4 (2.9 MB) would halve L3 traffic and footprint, but upstream measured it slower on Zen 2.
   - Any µs saved here is budget for (1)–(3).

**Not promising on the CPU side:** making the transformer itself L2-resident (impossible at 6M params), and
more PPMD memory (no RAM headroom).

## 8. Reproduce

```sh
patches/fx2-cmix-transformer/build.sh                # ~6.5 min; outputs in build/refs/fx2-cmix-transformer-v1/
L=/tmp/claude-1000/-home-mmarrujo-sub100mil/9d3b9113-c9c6-4d39-ac90-a720c8f61d37/scratchpad/bench.lock   # shared timing lock
P=patches/fx2-cmix-transformer
flock $L scripts/bench.sh --method 'fx2-cmix-transformer-v1 g++15 PGO (slice mode)' --slice data/slices/enwik9_1M \
  --comp "taskset -c 2 $P/slice_cmd.sh comp {in} {out}" --decomp "taskset -c 2 $P/slice_cmd.sh decomp {in} {out}" \
  --decomp-size 3601651
flock $L $P/tools/perfstat.sh   data/slices/enwik9_1M /tmp/perf1M     # perf stat (topdown, L2/L3/DRAM fills) + perf record
flock $L $P/tools/contention.sh data/slices/enwik9_1M /tmp/cont1M     # transformer-only vs membw co-runners
flock $L taskset -c 2 build/refs/fx2-cmix-transformer-v1/membw sweep  # single-core read bandwidth by working set
# per-model rdtsc breakdown: run cmix_prof -c english.dic <slicetool pre output> out --transformer 6m-q4-fp32.tfwc2
```

Notes:

- The transformer's own running loss on the 1M slice is 1.276 nats/token over 552K tokens. Training
  reported 0.911 nats/byte averaged over all of enwik9 [UP].
- The loss counts every stream byte, including:
  - the PPMD-predicted first token of each article;
  - the raw-XML coda (1.6% of the 1M stream, untransformed, as in -e);
  - the cold intro.
- The start of enwik9 is long human-written articles. The bot-generated census articles and the redirects
  that pull the whole-file average down come later.
- A wrong token mapping would show losses above 3 nats/token. Any slice result is a *relative* baseline for
  mechanism experiments, not a prediction of the full-file ratio.
- Raw logs of these runs (gitignored) are in `build/refs/fx2-cmix-transformer-v1/results/`: `prof1M/`,
  `perf1M/`, `contention.txt`, `membw.txt` and `bench_*.txt`.
