# cmix-neif-1.0: build, slice compatibility, timing and bottleneck characterization

Reference: `marwoelk/cmix-neif-1.0@1c9ea5e` (GPL-3.0-only), cloned at `third_party/cmix-neif-1.0` by
`scripts/fetch_refs.sh`. Pending entry, S = 95,914,051 (comp9 4,000,689 + archive9 91,913,343 + 19 B options).
Measured on the dev box: AMD Ryzen 5 8600G (Zen 4, 6C/12T, 1 MiB L2 per core, 16 MiB shared L3,
boost to 5.08 GHz), Fedora 43, g++ 15.3.1. All work products live in `build/refs/cmix-neif-1.0/` (gitignored).
Our changes to the code are in `patches/cmix-neif/0001-gcc-build-stream-mode-tfprof.patch`
(`git apply` on a copy of the tree, never on `third_party/`).

## 1. Audit (before running anything)

Read: `install.sh`, `build.sh`, `build_and_construct_comp.sh`, `makefile`, `environment.txt`,
`tools_src/packer/build_packer.sh`, `src/runner.cpp`, `src/readalike_prepr/self_extract.h`.

| script | network | root / sudo | writes outside the repo | verdict |
|---|---|---|---|---|
| `install.sh` | **yes**: `apt-get update/install` against `snapshot.ubuntu.com` (pinned 2026-09-27); on Ubuntu 22.04 it falls back to **`curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \| sudo gpg --dearmor`** and adds an apt source | **yes** (`sudo apt-get`, `sudo tee /etc/apt/sources.list.d/...`) | yes, system package state | **not run**. apt/Debian-only anyway; on Fedora it only prints the package list |
| `build.sh` | no | no | no: copies the package to `./.build`, runs the build under `env -i`, writes `./comp9`, deletes `.build` | safe, but needs clang-18/lld-18/llvm-18 tools, so it stops at its tool check here |
| `build_and_construct_comp.sh` | no | no | no | runs PGO (`-fprofile-generate` + two `./cmix -c` runs on `prof_input/`, only if no profile is shipped; one is), `make prof_use`, strip, GNU `objcopy`, `llvm-objcopy --strip-sections`, the packer, then appends blocks. **It ignores the exit status of the PGO profile runs** (see §3: the `-c dict` run on `prof_input/input` aborts) |
| `makefile` | no | no | no | clang-only flags (`-ffp-model=`, `-fno-direct-access-external-data`, `-fuse-ld=lld-18`, `--icf=all`), lld linker script `encoder_split.lds` |
| `tools_src/packer/build_packer.sh` | no | no | no | builds a freestanding stub with clang, runs `cut_dpath.py` (python3) over the linker map; the probe step executes the packed probe binary |
| runtime (`comp9`/`archive9`) | no sockets | no | **yes, outside cwd**: `tmpfile()` files in `/tmp` (copy of the coder stream for link context, about 0.55 GB at the end of a full run; 0.88 + 0.025 GB during `-e` preprocessing). In cwd: `ppm.page` (14,000 MiB sparse), `.dict`, the weight container, `*.cmix.temp`, dot-files of the `-e` split | note `/tmp` is a 16 GB tmpfs on this box; a full run fits, but `BENCH_TMP` and `/tmp` share it |
| runtime self-exec | — | — | — | `system()` re-runs its own image (`-D`/`-C`) through a shell to decode the embedded blocks and code the dictionary. Reads `/proc/self/exe` and `/proc/self/statm` |

No `curl | sh`. The only network and root access is `install.sh`, as its header says.

**Missing dependencies for the upstream (byte-identical) build.** The pinned toolchain is Ubuntu 24.04's
clang/lld/llvm **18.1.3** plus GCC 13 headers and glibc 2.39. Fedora 43 has compat packages for **18.1.8**, not 18.1.3:

    sudo dnf install clang18 lld18 llvm18 compiler-rt18     # 18.1.8-7.fc43 (compiler-rt18 is 18.1.8-4.fc42)

Even with these, the binary would not be byte-identical: a different clang point release, libstdc++ 15 headers and glibc 2.42
all move bytes (the authors say so in `install.sh`). A byte-identical `comp9` needs an Ubuntu 24.04 container
(`podman` is installed: `podman run ubuntu:24.04` + `install.sh` inside it, which needs network). We did **not**
do that; it only matters for checking their published sha256, not for any of our own experiments.

**Training environment (not set up).** Python/PyTorch training tree in `training/tf/` (Ivanov's trainer + neif
overlay, Muon + AdamW, bf16 then QAT), dump tools that are build variants of the compressor, and 228 GB of
PPMd-prior dumps plus 40 GB of BITLSTM trace (`training/README.md`, `training/MANIFEST.tsv`). The author reports
about 2,800 GPU-hours. BITLSTM is trained on a CPU by replay (32 passes over a 20% trace).

## 2. Build (g++ 15.3.1, no clang)

g++ builds the source **with no source changes needed for correctness**: about 7 s with `-j12`, LTO included. The
clang-only parts are either `#if defined(__clang__)`-guarded or are unknown pragmas that g++ ignores
(`#pragma clang loop unroll(full)` in `mixer_bank.h`). Our build is `makefile.gcc` (in the patch), which keeps
upstream's translation-unit groups and its FP-model split:

| upstream (clang 18) | ours (g++ 15) |
|---|---|
| fast units: `-ffp-model=fast -mrecip=none` | `-ffast-math -ffp-contract=fast -mrecip=!all -U__FAST_MATH__` |
| precise units (transformer `src/tf1/*`, BITLSTM, adaptive depth, detmath, vector math, optimizer, word label): `-ffp-model=precise -ffp-contract=off` | `-ffp-contract=off -fno-math-errno -mrecip=!all` (gcc's default is IEEE) |
| `-O3` fast / `-Oz` slow+cold / `-Oz` tf1 loader | `-O3` / `-Os` / `-Os` |
| PGO (shipped clang profile) + LTO on the fast group, lld `--icf=all`, `encoder_split.lds` | LTO (`-flto=auto`) on the fast group, **no PGO**, GNU ld, no ICF, no linker script |
| `-march=x86-64-v3 -mtune=znver2` | same. Never `-march=native` |

Three g++ pitfalls, all found by checking the binary rather than assuming:

1. **libmvec and reciprocal estimates.** With `-ffast-math`, glibc's `math-vector.h` declares SIMD variants and
   g++ vectorizes `log`/`exp` loops into `_ZGVdN8v_logf`-style libmvec calls. Those results depend on the glibc
   version, which is exactly what neif's determinism rules forbid (README §1.7.2). `-U__FAST_MATH__` removes them.
   g++ 15 also treats `-mrecip=none` like the default (vrcpps/vrsqrtps still emitted, checked with a 3-line test),
   so we use `-mrecip=!all`. One scalar `vrsqrtss`+Newton remains in `LstmLayer` (1/sqrtf in Adam): gcc emits it under
   `-funsafe-math-optimizations` whatever `-mrecip` says. It is deterministic on one CPU, so it matters only when
   decoding on a different CPU vendor. Final binary: no `zmm`, no vector rcp/rsqrt, libm imports only `sincosf` and `sqrt`.
2. **Start-up build check aborts.** `MixerInput::MixerInput` checks `eps != min_ || (1 - eps) != max_`, and gcc's
   reassociation folds it differently from clang, so the program aborts although the printed values agree.
   `mixer-input.cpp` gets `-fno-associative-math -fno-reciprocal-math` (its hot code is inline in the header).
3. **AUX helper.** The hint package and article-order blocks are coded by a second build with `-DAUX_BUILD=1`
   (`make -f makefile.gcc OUT=cmix_aux O=obj_aux AUX=1`). They must come from *our* helper, because the shipped
   program decodes them with its own arithmetic.

**Sizes.**

| artefact | bytes | note |
|---|---:|---|
| `cmix` (g++, LTO, not stripped) | 751,536 | the binary we run |
| same, `strip --strip-all` | 654,536 | xz -9e: 290,644 |
| upstream packed program part of `comp9` | ≈182,723 | 4,000,689 − hints 57,503 − order 43,780 − weights 3,716,667 − header 16, assuming their hint block is also 57,503 |
| our coded hint package (`comp_hints`) | 57,503 | README: "about 57 KB compressed" |
| our coded article order (`comp_order`) | **43,780** | README: 43,780 exactly. The CM coder with full alphabet reproduces their size under g++ |
| weight container | 3,716,667 | unchanged input, counted twice in S |
| our unpacked self-extracting `cmix` (binary + hints + order + weights + header) | 4,569,502 | vs official `comp9` 4,000,689: **+568,813 B, all of it the missing CM-packed x86 stub** (654 KB stripped vs about 183 KB packed). Their packer (`tools_src/packer`) needs clang + llvm-objcopy + the lld linker script and was not attempted |

**Bit-exactness.** A g++ build is **not** expected to reproduce their `archive9`. The fast units are compiled with
fast-math semantics, so *any* compiler or optimizer difference (vectorization width choices, reassociation, FMA
contraction, inlining under LTO, even a different PGO profile, see their own `stream_rate.tsv` note) changes
float results in the mixers and context maps, and so the coded bits. The precise units (transformer, BITLSTM)
are written with explicit intrinsics and strict FP, so they are *likely* identical across compilers, but the
inputs they receive from the fast units are not. **For us it does not matter.** Every round trip we run uses one
binary on one machine for both directions, and that is deterministic. It matters only for comparing against
their published `archive9` sha256, or for decoding one of their archives with our build (that would fail).
The CM-only AUX path reproducing their 43,780 B order block exactly suggests the integer-heavy CM core still
matches closely.

## 3. Slice compatibility: enwik9-only, so we run prefixes of its coder stream

The program has four modes (`cmix -e enwik9 out | -c [dict] in out | -d [dict] in out | -h a b c`; no args =
self-extract). Only `-e` on the real enwik9 is supported, and the generic `-c` mode is not generic:

| attempt | result |
|---|---|
| `-c dict prof_input/input` (the PGO input shipped with the package) | aborts: `metablock: page id delta not numeric`. The metadata-block automaton expects neif's page-form stream. The upstream build script ignores this exit status |
| `-c prof_input/input` (no dictionary) | aborts: `transformer error: byte 0x3c is not in the frozen byte set` |
| `-c dict data/slices/enwik9_1M` | aborts: `metablock: page id delta not numeric` (raw XML would next hit `<`, which is not one of the 208 stamped symbols) |

Everything below the coder is tied to full enwik9 by construction (README §4.6: "An input other than enwik9 is not
supported"): the stamped 208-value alphabet with no escape, the article-order residual (one entry per enwik9 article),
the dictionary *derived from enwik9* plus a hint package, the census transform, the metadata block that
`metablock.h` checks against hard-coded `3 + 2 x 243,425` lines, and the gate, prompt and alias tables stamped from
the full stream.

**Smallest change that makes slices work: a stream mode** (`runner.cpp`, about 30 lines, all behind env vars, so the
default behaviour is unchanged):

* `NEIF_DUMP_STREAM=<path> cmix -e enwik9 x`: runs the whole `-e` preprocessing (dictionary derivation 55 s, census,
  split, comment index, reorder, phda9, WRT, metablock and ip packing: about 3 min) and writes **exactly the bytes
  `Compress()` would code**, then exits.
  Our dump is 552,750,558 B with exactly 208 distinct values (the README says 552,750,198: **+360 B, 0.00007%**,
  unexplained, perhaps a g++ difference in a float-using preprocessing step). Note: the `-C` child process that codes
  the dictionary inherits the variable and writes its own stream to the same path first; the main process overwrites it.
* `NEIF_STREAM_MODE=1 cmix -c dict <stream-prefix> out` / `-d dict out dec`: codes a stream file with the full model
  (dictionary pretraining, grammar automaton, metadata/alias/author/timestamp automata, prompts, transformer, BITLSTM)
  and skips only the text transforms on either side. The decoded output is the stream prefix itself, so
  `bench.sh`'s `cmp` verifies it.
* Two end-of-stream guards fire on a prefix that ends inside the metadata block or inside a digit run
  (`metablock::ByteWacht`, `CoderPrompt::EndArticle`). In stream mode they are skipped. Both are checks, not coding steps.

Slices (`build/refs/cmix-neif-1.0/slices/`, sha256 in `SHA256SUMS` there): `neif_stream_1M`, `neif_stream_10M`,
`neif_stream_24M` = the first 10^6, 10^7 and 2.4x10^7 bytes of the dump.

**What is and is not representative.** The stream starts with neif's restructured layout (README §4.5):

| stream bytes | content | transformer |
|---|---|---|
| 0 – 5.16 M (0.93%) | metadata block (page ids, revision ids, timestamps, contributors) | **gated off** (CM only) |
| 5.16 – 8.81 M | redirect block (one line per page) | on, redirect runs form one document |
| 8.81 M – 456 M | ordinary articles, incl. the calendar block | on, with prompt injection |
| 456 – 528 M / 528 – 553 M | census pages / image, `Wikipedia:`, disambiguation pages | on |

* `neif_stream_1M` is **pure metadata block**: CM only, no transformer. A smoke test and nothing else.
* `neif_stream_10M` is metadata + redirects + the first 1.19 MB of articles (the console marks "entering redirect
  block at 51.64%" and "entering main text block at 88.08%" of this slice).
* `neif_stream_24M` has about 15.2 MB of ordinary articles, the first that the stream offers. That is the only
  region representative of the 90% of the payload that matters (LANDSCAPE §3.1). Even there the early articles have less
  link context, fewer aliases and a colder PPMd, CM and online LSTM than mid-stream, and BITLSTM stays frozen until
  20% of the full stream (110 MB), so a prefix never exercises its online learning.
* **Compression ratio** is directly comparable with neif's own `doc/stream_rate.tsv` at the same stream positions,
  and that is the check we use. Our raw `enwik9_*` slices cannot be compared at all: neif never sees raw XML.

## 4. Results

g++ 15.3.1 build (no PGO), stream mode, single thread. "Reference" = neif's own run, `doc/stream_rate.tsv`
(clang PGO build, slightly different profile), cumulated and linearly interpolated inside its 0.1% windows at the
same stream position. The ledger rows are `cmix-neif-1.0 g++15 (stream mode)`. The two earlier `FAIL` rows for
`neif_stream_1M` are the two end-of-stream guards of §3, before they were skipped.

| slice (coder-stream prefix) | content | ours (B) | reference (B) | Δ | bits/stream B | comp s | decomp s | peak RSS | round trip |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| `neif_stream_1M` | metadata block | 339,336 | ≈339,056 | +0.08% | 2.715 | 114.4 | 110.7 | 6.05 GB | **yes** (bench.sh) |
| `neif_stream_10M` | metadata + redirects + 1.19 MB articles | 1,918,894 | ≈1,920,889 | −0.10% | 1.535 | 2,327.5 | 2,305.9 | 6.05 GB | **yes** (bench.sh) |
| `neif_stream_6M` ×3 | metadata + 0.84 MB redirects | 1,493,809 | ≈1,490,880 | +0.20% | 1.992 | 856 / 1,343 / 1,586 | — | 6.2 GB | comp only, pinned (contention runs, §6) |
| `neif_stream_24M` | + 15.2 MB articles | 4,209,622 | ≈4,212,327 | −0.06% | 1.403 | 6,195 (under perf) | — | 9.55 GiB | comp only (profiling run) |

The g++ build tracks neif's own coded size within ±0.2% at every checked position. Decompression costs the same as
compression (2,306 s vs 2,328 s on 10M). Peak RSS climbs to the designed ~9.5 GiB as the PPMd page pool grows to its
4 GiB cap (README §4.8).

**Speed per region** (from the timestamped 500 KB progress lines, µs per coder-stream byte):

| region | 10M run | 24M run |
|---|---:|---:|
| metadata block (CM only, transformer gated) | 94.8 | 86.5 |
| redirect block (about 1.93 transformer steps per byte: page rows + redirect rows dominate) | 413.9 | 418.1 |
| ordinary articles (first 15 MB; about 1.05 steps per byte) | 254.9 (0.7 MB) | **276.4** (14.8 MB) |

**Full-enwik9 ESTIMATE on this box:** 5.16 MB × 86.5 + 3.65 MB × 418 + 543.9 MB × 276 µs + about 5 min of setup
(dictionary derivation 55 s, preprocessing about 3 min, pretraining 20 s) ≈ **152,600 s = 42.4 h per direction,
152,600 ns per enwik9 byte**. Official on EPYC 7B13 (Zen 3, GB5 1306): 175,392 ns/B = 48.72 h, i.e. 317 µs per stream
byte on average. So this box with our no-PGO g++ build is about **0.87×** the official time (ESTIMATE). Caveats:
* The article rate comes from the *first* 15 MB of articles. Later articles carry more link context and alias rows
  (more transformer steps per byte), while census pages (13% of the stream, 0.43 bits/B) let adaptive depth skip more.
  neif's own run spread 0.23–0.86 h per 1% of the stream.
* It assumes the transformer's L3-resident working set survives (§6). With a noisy neighbour this box gets 1.6–1.9× slower.

## 5. Where the time goes

perf, `cycles:u` at 499 Hz plus `ls_dmnd_fills_from_sys.dram_io_near` sampling, on the `neif_stream_24M` run. The
article window is perf's last 67% of wall time, which starts at the "entering main text block" mark. Symbols are
grouped by name (`build/refs/cmix-neif-1.0/tools/categorize.py`). LTO inlines the top-level cmix mixers, SSE and adaptive depth into
`Predictor::Predict/Perceive`, so they appear as "predictor".

| component | cycles, whole 24M run | cycles, article window | DRAM demand fills, article window |
|---|---:|---:|---:|
| **transformer** (`fx2::opt::*`, prompt/gate/link-context code) | 57.4% | **58.9%** | 5.5% |
| fxcm text model (`fxcmv1::*`: context maps, its mixers and APMs) | 24.5% | **24.2%** | **55.9%** |
| predictor: layer-0 + final mixers, SSE, adaptive depth (inlined) | 6.2% | 5.7% | 19.3% |
| online 80-cell byte LSTM (+ its Adam/backward helpers) | 5.1% | 4.6% | 11.7% |
| PPMd | 1.9% | 1.8% | 1.3% |
| BITLSTM | 0.9% | 1.1% | 0.6% |
| named mixers/SSE outside the predictor + other CM models | 1.9% | 1.8% | 3.8% |
| detmath `LogCore`, libc, kernel, misc | 2.0% | 1.9% | 1.9% |
| preprocessing (WRT, census, reorder, dictionary derivation; once per run) | ≈0.2% of a full run (about 5 min of 42 h) | — | — |

Top symbols, article window: `qgemv_relu2q` (MLP up) 11.0%, `pv_int_pairs` (SWA PV) 9.0%, `qgemv_f32` 8.6%,
`fxcmv1::ContextMap3::mix` 8.0%, `attn_step_fixed` 5.9%, KDA `sweep_head_t` 4.9%, `kda_layer_step` 4.1%,
`EKMC6::get` (compact context-map buckets) 6.0%, `Predictor::Predict` 3.6%, `sparse4_accum` (MLP down) 3.5%,
`fxcmv1::Mixer1::p1` 2.9%. **DRAM** demand fills: `Mixer1::p1` 26%, `Predictor::Predict` 19%, `ContextMap3::mix` 11%,
`LstmLayer::BackwardPass` 8%, `EKMC6::get` 9%.

Inside the transformer (built-in rdtsc profiler, `-DFX2_PROF`, per step averaged over 23.06 M steps, of which 18%
are inserted prompt rows and 18% kv-only):

| part | share of transformer time |
|---|---:|
| int8 matmuls (MLP up 18%, q/k/v projections, gates, out projections, MLP down sparse, unembedding) | 46.3% |
| SWA attention, 4 layers × W=2048 (QK, softmax, PV) | 35.1% |
| KDA recurrence, 12 layers | 16.0% |
| norms, quantization, residuals, head | 2.7% |

**µs per token: 152.8 per transformer step** on average (156.8 full, 133.9 kv-only), i.e. about 700 K core cycles at
4.57 GHz. Per article byte that is about 161 µs (1.05 steps per byte), consistent with 58.9% of 276 µs. LANDSCAPE.md's
estimate of about 140 µs/token on EPYC was the right order of magnitude. The author's "about 44% of run time" was for
an earlier 12-layer model; with the shipped 16-layer, W=2048 model it is about 59% here.

## 6. Is transformer inference memory/cache-bandwidth bound or compute bound?

**Verdict: L3-bandwidth bound when its working set is resident, DRAM-bound as soon as it is not.** The dominant
part (the int8 matmuls, 44–46% of transformer time) streams the whole weight arena once per token at exactly the
measured L3 read bandwidth of this box. Taking away L3 capacity, without adding work, makes the transformer
2.25–2.9× slower.

**Footprint per token** (printed by the instrumented build, `SUB100_TFPROF`):

| per-token stream | bytes | vs L2 (1 MiB/core) | vs L3 (16 MiB shared) |
|---|---:|---|---|
| weight arena (int8, **unpacked** from the 4-bit container at start-up; group headers included) | 7,341,888 | 7.0× | 44% |
| SWA K+V rings (4 layers × 2048 × 192 × 2, int8) | 3,145,728 | 3.0× | 19% |
| KDA states (12 layers × 3 heads × 64×64 fp32 + conv history) | 701,184 | 0.67× | 4% |
| **total read per full token** | **≈11.2 MB** | 10.7× | **67%** |

Nothing of this fits in L2. All of it fits in a 16 MiB L3, *if* the context-model bank (GBs of hash tables with random
access) leaves it alone between tokens.

**Bandwidth microbenchmark** (one core, AVX2 sequential reads, `bw.c`, pinned): L1 230 GB/s; L2 115–138 GB/s;
L3 at 1–12 MiB 103–117 GB/s; 16 MiB 83 GB/s; DRAM (≥64 MiB) 45–48 GB/s.

**In-situ measurements** (`neif_stream_6M`, comp only, neif pinned to cpu3; antagonist `bw stream` pinned to cpu5,
another physical core sharing the L3; same binary, same output 1,493,809 B, same instruction count 9.217e12 in all
three runs):

| run | wall s | tf s | µs / tf step (full / kv-only) | matmul µs/token → effective GB/s of the 7.34 MB arena | SWA attention µs/token | KDA cycles/token (TSC) | DRAM fills, all (GB/s) | IPC |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | 856 | 250 | **154** (167 / 140) | 65.8 µs → **111.6 GB/s** | 55.8 | 102 K | 0.87 | 2.36 |
| + 12 MiB L3-resident antagonist | 1,343 | 563 | **347** (376 / 314) | 183 µs → 40.1 GB/s | 111.6 | 174 K | 6.34 | 1.59 |
| + 256 MiB DRAM-streaming antagonist | 1,586 | 721 | **444** (500 / 381) | 238 µs → 30.8 GB/s | 146.3 | 210 K | 6.02 | 1.33 |

Reading the evidence:

1. **Matmuls run at L3 speed.** 7.34 MB in 65.8 µs is 111.6 GB/s, the measured single-core L3 read bandwidth
   (103–117 GB/s). In core cycles (4.57 GHz average) that is 24.5 B/cycle = 24.5 int8 MAC/cycle. Zen 3/4 issue two
   256-bit `vpmaddubsw` per cycle (64 MAC/cycle; from instruction tables, not measured here), so the ALUs are at
   about 40% while the load stream is saturated. It
   matches Ivanov's own Zen 2 measurement of these kernels ("~20–22 MAC/cyc ... at the model's ~6 MB L3 footprint",
   `cpp_infer/bench/results/gemv_*.txt`).
2. **A 12 MiB antagonist that fits in L3 by itself** (it uses almost no DRAM bandwidth alone) slows the transformer
   2.25×. 11.2 MB + 12 MiB > 16 MiB, so the weights are evicted between tokens and refetched from DRAM. The process's
   own DRAM fills go from 0.87 to 6.3 GB/s. This is a **capacity** effect, not a compute effect: the instruction count is unchanged.
3. **The 256 MiB DRAM streamer** both evicts and competes for DRAM bandwidth: 2.9× on the transformer, 3.6× on the matmuls.
4. **SWA attention** (37% of transformer time) moves 2.7 MB of KV per token in 55.8 µs (49 GB/s), below L3 bandwidth.
   Ivanov's notes say the int8-V PV product is conversion-pipe-bound (`vpmovsxbd` + `vcvtdq2ps`), so it is partly
   compute-bound. It still slows 2.6× under contention, so its K stream is cache-sensitive too.
5. **KDA** (16%) has a 48 KB state per layer, so its state traffic stays mostly L2 or L3. It is the most compute-like part
   (about 9 K core cycles per layer-token, against a roughly 3 K FMA-throughput floor) and still slows 1.7–2× under contention.
6. **The CM side is memory-latency sensitive too:** the non-transformer time of the same run grows by 29–43% under the
   antagonists (606 → 780 / 864 s).

**Consequences for the reference machines (ESTIMATE).**
- Ryzen 7 3700X (Zen 2): 16 MB L3 per 4-core CCX, so it fits, as here.
- EPYC 7B13 cloud VM: a 32 MB CCX L3 shared with neighbours.
- **Intel i7-1165G7: 12 MB L3 in all.** That is less than the 11.2 MB transformer working set plus any CM traffic.

That plausibly explains part of neif's 82.87 h decompression on the Intel box against 48.7 h on EPYC (1.70×; our
L3-thrash run gives 1.57× on the whole run). Some of it may be the 12 GB WSL2 memory-mapping problem that also hit the record.

## 7. Where CPU-side mechanism changes could plausibly fit

Frame: neif runs 48.7 h against a 53.6 h limit (about 5 h of headroom on EPYC), and about 59% of article time is a
transformer that is bound by L3 bandwidth and L3 capacity. Any model we add has to be paid for with speed, so
speed-ups that are **bit-exact** (no retraining, archive unchanged) come first. Ranked by our estimate of value per
effort. All are ESTIMATES, to be measured on `neif_stream_24M`.

1. **Batch the inserted prompt rows.** Rows the decoder already knows (page rows, synonyms, link context ≤256 tokens,
   aliases, heading reminders) are fed one token at a time today, so every row streams the full 7.3 MB of weights.
   A block of k known rows could run as one int8 GEMM, reading each weight once per block. The int8×int8→int32 dot
   products are exact, so the result is **bit-identical** as long as each row's accumulation stays the same, and no
   retraining is needed. The KDA recurrence and the causal attention stay sequential per token; only the projections and
   MLP (46% of transformer time) batch. Inserted rows are 18% of steps on the 24M prefix: 48% in the redirect block,
   about 5% in the first articles. The share grows where link context is dense. Value: up to ~5–10% of transformer
   time where rows are dense; small on early articles.
2. **Shrink the transformer's L3 footprint.** Today 11.2 MB of the 16 MiB L3 is transformer state, and a 12 MiB
   co-runner already costs 2.25×. Options that keep outputs identical:
   * Packed int4 weights (3.7 MB instead of 7.3 MB). Same integer values, so exact. Ivanov measured packed int4
     slower when L3-resident on Zen 2 (18.6 vs 21 MAC/cyc) but 2× faster once spilled (7.75 vs 4.16 MAC/cyc). Zen 4
     has twice Zen 2's integer SIMD width, and on a 12 MB-L3 Intel box everything spills, so it is worth re-measuring.
   * Freed L3 also speeds the CM side, which takes 80%+ of DRAM misses.

   This is also a *robustness* fix for the reference machines (§6).
3. **CM-side memory latency.** fxcm + predictor + online LSTM cause about 87% of DRAM demand fills, though they are only
   about 35% of cycles. `fxcmv1::Mixer1::p1` alone causes 26% of DRAM fills for 2.9% of cycles: its weight rows are
   selected by contexts known well before use, so software prefetch at the byte boundary (neif already does this
   for some maps, README §3.4: "prefetches at different places compete") is the obvious first test. Value: a few % of
   total time, bit-exact.
4. **SWA attention compute** (35% of transformer time, partly conversion-bound). Ivanov's `AttnKVF32` variant (fp32 V)
   is bitwise-identical and was faster on Zen 2, but at W=2048 it costs +3.4 MB of L3 against item 2. A per-head
   score-skip threshold (`skip_threshold`, off today) changes outputs, so it would need a measurement, not a retrain.
5. **Spend the time saved on CPU-side models**, which is our plan of record:
   * more online adaptation behind the frozen transformer. BITLSTM is 1.1% of time for about −0.3 MB, by far the best
     ratio in the build. A wider or better-fed BITLSTM, or ex-girlfriend's online fast-weight head (−140 KB of S for neif),
     both need no GPU.
   * more context-model capacity, now that PPMd takes only 1.8% of time.

   The 80-cell byte LSTM is a poor place for more time: the author measured 96/112 cells at "a few KB" for about +1 h each.
6. **PGO with g++** (their profile gave −2.7%) and a clang build (the per-file FP model is tuned for clang) are easy
   speed checks before any mechanism work. They matter only for our time estimates, not for size.

Not CPU-side, for completeness: a larger window (2560: −13 KB at 39%) or more layers needs retraining.


## 8. neif vs record: which mechanisms differ, what each is worth, and what ports without retraining

The record (fx2-cmix-transformer) is fx2-cmix with the online LSTM replaced by a 12-layer transformer
(9 KDA + 3 SWA, W=1024, 205-symbol vocabulary, d=192, about 6M params), with only the **PPMd next-byte distribution**
as its side input and the context reset per article. Its writeup says nothing else changed apart from three bug fixes.
neif is cmix-lex plus a lot more. Values below are the **author's own figures** (README §3; measured on prefixes,
windows or partial runs, explicitly sub-additive) and the payload deltas are for neif's stream, not the record's.

"Retrain?" = does porting it onto the record require retraining the record's transformer (GPU, out of budget by
default). The test: does it change *what the transformer sees* (its input bytes, its side inputs or their
distribution)? A CM, mixer or post-mixer change does not. A stream or PPMd change does.

| mechanism (neif) | README value | retrain? | note for porting |
|---|---|---|---|
| **BITLSTM** (16-cell LSTM correcting the final logit, after the mixer) | about −0.3 MB vs none (estimate at 32%); −44 KB vs online-only 32-cell; +3–4% time | **no GPU**. Offline weights need a CPU replay trace of *our* run (32 passes on a 20% trace, CPU only); an online-only BITLSTM needs nothing | strongest no-retrain item. Inputs: 13 mixer columns, final-mixer output, a fixed random projection of the transformer hidden state (192) and the byte-LSTM state. Upstream origin is cmix-obias `bitlstm32` |
| **small online byte LSTM** (80 cells, block-diagonal) beside the transformer, + **PPMd logit prior** (×0.55) + hashed 2/3-byte and match inputs | removal about +12 KB at 30% (no BITLSTM); prior −19 KB at 80 MB; inputs about −60 KB (older config) | no | the record dropped the LSTM entirely. This puts back a *cheap* online model that adapts where the frozen transformer can't. About +8% time for 64 cells |
| **adaptive depth** (zmix): skip mixers/SSE/BITLSTM when a 33-weight logistic is sure (\|logit\|>8, 39% of bits) | +2.7 KB per 80 MB (cost); **−5.1% instructions** | no | pure time win to spend elsewhere |
| end-to-end mixer gradient (zmix) | −10.1 KB / 80 MB | no | |
| word-label log-bilinear model (zmix) | −4.3 KB / 80 MB (−6 to −14 KB full) | no (needs the dictionary code-word tree, which the record also has) | |
| second match channel (fx-deepmix), indel recovery (zmix) | −26 KB full; −4.3 KB / 80 MB | no | |
| mixer learning rates, schedule restart, SSE flag widths, direct-state-map widths, context-map capacities and bucket form | −33/−46 KB, −2 KB, −6.5 KB, −39 KB, −5 KB, ... | no | tuning items. The values are stream-specific, so re-tune on our stream |
| ISBN check digit, number/date contexts, APM extra contexts, UTF-8 contexts | −10..−13 KB, about −60 KB (est.), not isolated | no | CM additions |
| speed items: mixer bank, small logit table, branch-free CM states, prefetch, huge pages, build constants, PGO profile on the real stream | −6%, −4.5%, −2%, −2.4%, −1.7%, −3%, −2.7% | no | pay for other models. **Our profile (§5, §7) shows where these land** |
| PPMd paging (own 4 KiB frame pool) | 0 B, memory | no | lets more RAM go to models |
| **integer attention kernel** for the SWA PV product | in the noise; −5.9% instructions | **borderline**: changes transformer numerics slightly but needs no new training. Measure the payload |
| output skip for unread positions | −0.3% | no, but only useful together with prompt rows |
| **PPMd order limit per context** (8..25 by byte classes) | −20 KB (without transformer) | **yes** in principle: the PPMd distribution is a transformer *input*, and neif's model was trained on these priors. Porting it to the record shifts the input distribution. Measure first, the mismatch may be small |
| PPMd restart + warm start after the metadata block | −7.1 KB / 80 MB (no tf) | same caveat (changes the prior) |
| **prompt injection** (page id/number/contributor/date rows, synonyms, link context, aliases, heading reminders, digit-run classes) | not isolated. A bug dropping rows cost 167 KB at 20% (≈0.8–0.9 MB extrapolated) for a model *trained with* them | **yes** | the record's model never saw prompt rows |
| side channels into the transformer (two previous fed tokens, match byte + length class) | in model figures | **yes** | new embedding rows |
| gate (metadata block withheld from the transformer) | not measured | **yes** (the record's training stream differs) | |
| page form without XML, metadata block at head + split/sort, field forms, timestamp forecast, user id at first mention, contributor feed, redirect mark codes, census transform, `&sup2;` control byte, comment index, language links folded in, larger dictionary, dictionary hints, order rule + residual, calendar block, redirects in front | −17.5 MB stream (time), −62 KB, −20 KB, −45 KB, ... −41 KB, −64/−29 KB, −106/−19 KB, −50 KB of S, −100 KB of S, +90 KB for redirects | **yes for every item that changes bytes the transformer sees**. Items that only change side information shipped once (dictionary hints, order rule + residual: −50 KB and −100 KB of S) are stream-neutral | the hint and order-residual packaging tricks port cleanly. Everything else is a stream change |
| frozen 208-symbol alphabet | −4 KB, −4% instructions | yes (vocabulary) | the record's equivalent would be a stamped 205-set: same idea, no retrain, if done with its own symbol set |
| bigger transformer (16 layers, 7.8M), window 2048 | the bulk of the A gain; W2048 −48.6 KB at 39% | **yes** | |
| own CM x86 SFX stub, container v3/v4, dead-neuron removal, encoder-only code carried once, size-optimized compile | −101 KB, −115,514 B/copy, −21,922 B, −22 KB, −36 KB of S | **no**. Container re-coding is lossless, and dead-neuron removal and zeroed collapsed channels left the archive byte-identical | cheap S wins on the record: roughly −0.3 MB of S together (ESTIMATE, sum of the listed items) |

**Cheap to port onto the record without retraining:** BITLSTM (online-only immediately; offline weights from a CPU replay
of our own trace), the small online byte LSTM with PPMd prior, the zmix mixer/CM items, the speed items, and all
packaging items (stub, container v4, dead neurons, encoder-only code). Together the author's figures suggest
**several hundred KB of payload plus about 0.3 MB of S** (ESTIMATE, sub-additive). **Needs retraining:** prompt injection,
side channels, gate, any stream/preprocessing change, the PPMd order table (or accept a train/test mismatch and
measure it), window and size.

## 9. Reproduce

```sh
B=build/refs/cmix-neif-1.0; mkdir -p $B
(cd third_party/cmix-neif-1.0 && tar cf - --exclude=./.git --exclude=./training .) | (mkdir -p $B/tree && cd $B/tree && tar xf -)
(cd $B/tree && git apply ../../../../patches/cmix-neif/0001-gcc-build-stream-mode-tfprof.patch)   # or patch -p1
(cd $B/tree && make -f makefile.gcc -j12 && make -f makefile.gcc -j12 OUT=cmix_aux O=obj_aux AUX=1 \
            && make -f makefile.gcc -j12 OUT=cmix_prof O=obj_prof XDEFS="-DSUB100_TFPROF -DFX2_PROF")
# self-extracting layout (what build_and_construct_comp.sh does, without the clang stub packer)
mkdir -p $B/pkg && cd $B/pkg && cp ../tree/cmix cmix_orig && cp ../tree/weights-*.c4 .
../tree/cmix_aux -c ../tree/dictionary/english.hints comp_hints
../tree/cmix_aux -c ../tree/src/readalike_prepr/data/new_article_order comp_order
W=weights-poc8m_w2048_p15f_v3bf16_gestrichen_v4.c4
./cmix_orig -h $(wc -c <comp_hints) $(wc -c <comp_order) 0 $(wc -c <$W) && cat cmix_orig comp_hints comp_order $W header.dat > cmix
# dump the coder stream (about 3 min), cut prefixes
mkdir -p ../dump && cd ../dump && ln -sf ../../../../data/raw/enwik9 . && NEIF_DUMP_STREAM=$PWD/neif_stream.full ../pkg/cmix -e enwik9 x
head -c 24000000 neif_stream.full > ../slices/neif_stream_24M     # etc.
# bench (run from a dir holding the weight container; it writes ppm.page, .dict there)
flock <lock> scripts/bench.sh --slice $B/slices/neif_stream_10M \
  --comp  "NEIF_STREAM_MODE=1 $B/tree/cmix -c $B/tree/dictionary/english.dic {in} {out}" \
  --decomp "NEIF_STREAM_MODE=1 $B/tree/cmix -d $B/tree/dictionary/english.dic {in} {out}" ...
```

The profiling and contention runs are `build/refs/cmix-neif-1.0/profile_runs.sh` (pinning, perf stat/record,
antagonist). The bandwidth sweep is a 40-line AVX2 read loop (`build/refs/cmix-neif-1.0/tools/bw.c`: `bw sweep`, `bw stream <MiB> <s>`); the perf symbol grouping is `tools/categorize.py` there.
