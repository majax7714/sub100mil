# P1: packaging and size (decompressor bytes, archive unchanged)

Type: **packaging** (no model change). Base: C3 (`patches/exp/C3-combo`). Date: 2026-11-07.
Goal: make comp9 (and the decompressor part of archive9) smaller while the archive stays bit-identical.

S accounting (docs/RULES.md §1, `self_extract.h`): comp9 = exe + comp_dict + comp_order + weights + 16-byte header;
archive9 = exe + comp_dict + weights + payload + header. So **every byte of exe, dict or weights counts twice in S;
the order stream counts once.**

## 0. The submission build: C3-full (patches/exp/C3-full, switches baked, PGO trained with the dictionary)

Where C3-full's +242,968 B over the record's exe comes from (612,760 vs 369,792; a symbol build of C3-full's own tree
has a byte-identical .text):

| source | raw bytes | packed share (xz of that code alone) |
|---|---:|---:|
| std::regex, used only by E12 | +70,889 | +29,082 |
| E12 own code | +45,817 | +18,160 |
| E3 bit_head | +33,949 | +15,886 |
| X2 own code | +17,242 | +9,116 |
| fxcmv1 (E9) | +10,856 | +3,715 |
| Predictor glue (E1/E2/E3/E12/X2 hooks) | +10,711 | +4,787 |
| fx2 transformer (E2 head) | +5,498 | +3,060 |
| other code (mostly std map/string/vector instances for E12/X2) | +19,785 | +10,265 |
| .eh_frame + .eh_frame_hdr | +14,316 | |
| .rodata | +8,784 | |
| dynamic linking, .data, alignment | ~+5,100 | |

So it's code, not tables (.rodata grew only 8.8 KB; nothing large is static data), and not PGO bloat in the
dictionary-trained build (the no-dictionary training's extra 74 KB was PGO: partial training keeps never-run code at
-O3). Total compressed growth is about +94 KB per copy, ~+188 KB of S.

| C3-full, measured | exe | comp9 | S vs C3-full unpacked |
|---|---:|---:|---:|
| unpacked | 612,760 | 3,843,810 | 0 |
| upx -9 (upstream's choice) | 286,808 | 3,517,858 | −651,904 |
| **upx --ultra-brute --lzma (UPX alone, now)** | **247,788** | **3,478,838** | **−729,944** |
| p1cm stub alone (no source change) | 225,152 | 3,456,202 | −775,216 |
| **P1 full** (`build_c3full_p1.sh`: + regex-free E12, bit_head -O2, no unwind tables, rank-delta order, FX2TFWC3) | **193,384** | **3,377,271** | **−913,717** |

S counts exe, dict and weights twice and the order once. P1 full is 183,773 B of S better than UPX alone, and its
comp9 is 51,203 B smaller than the official record's (3,428,474).

**Verified (fast lane, ledger rows `fx2T+C3-P1-*`):** every packed/shrunk build round-trips and its archive is
byte-identical (cmp) to the C3 control: enwik9_1M 126,244 B (unpacked, upx, p1 stub, P1 final, C3-full+P1; the
C3-full+P1 archive also equals C3-full's own `identity/clean/comp.bin`) and enwik9_mid1M_pa 29,759 B (C3 control,
P1 final, C3-full+P1). Speed: instruction counts were not completed (perf_cmp.sh failed on a relative path after the
first binary); contended wall times of the packed builds are not slower than the control (1M comp 230-264 s vs 308 s).
Rebuilding the stub with the current p1cm.h (no .data) saves another ~64 B. Its CM-coded dictionary stream is byte-identical to
C3-full's (`comp_dict`), the order stream equals the one C3 makes from the rank-delta file.

## 1. Size breakdown of C3's comp9 (unpacked, g++15)

| part | bytes | in S |
|---|---:|---|
| exe (`build/exp/C3/bin/cmix`, stripped) | 604,536 | ×2 |
| comp_dict (english.dic CM-coded by the exe, C3 env) | 99,889 | ×2 |
| comp_order (article order CM-coded, C3 env) | 200,493 | ×1 |
| comp_tfweights (FX2TFWC2, as-is) | 2,930,652 | ×2 |
| header | 16 | ×2 |
| **comp9** | **3,835,586** | (record build: 3,601,651; official, UPX'd clang: 3,428,474) |

Without the C3 env the dict/order streams are 100,036 / 200,917.

Exe by module (`.text` symbols of a symbol build whose `.text` is byte-identical to C3's; "xz" = that module's code
alone through xz --x86 -9e, a rough share of the packed size):

| module | C3 raw | C3 xz | record raw | record xz |
|---|---:|---:|---:|---:|
| other (runner, preprocessor, phda9, coder, glue) | 118,373 | 58,216 | 98,968 | 48,176 |
| fx2 transformer (cpp_infer) | 88,222 | 32,718 | 82,724 | 29,639 |
| **std::regex (only E12 uses it)** | **70,840** | **29,016** | 0 | 0 |
| fxcmv1 | 52,049 | 28,601 | 51,924 | 28,486 |
| E12 arith (own code) | 47,094 | 18,674 | 0 | 0 |
| Predictor | 47,001 | 23,583 | 38,192 | 19,518 |
| bit_head (E3) | 34,662 | 16,140 | 0 | 0 |
| X2 time law | 17,248 | 9,109 | 0 | 0 |
| ppmd | 16,377 | 10,384 | 16,361 | 10,383 |
| **sum** | 491,866 | 226,441 | 288,169 | 136,202 |

Other sections in C3: .rodata 40,232, .eh_frame + hdr 35,324, dynamic linking ~14 KB.

**C3's additions cost about 90 KB of compressed code per copy, about 180 KB of S** (E12 + regex ~50 KB, E3 ~16 KB,
X2 ~10 KB, the rest spread). The batch-3 estimates ("about +3 KB of S" for E12, "about 2 KB" for X2) left that out.

## 2. Executable packing

UPX 5.1.1 (installed 2026-11-07) on the C3 exe, deterministic (two runs byte-identical):

| packer | C3 exe | record exe |
|---|---:|---:|
| none | 604,536 | 369,792 |
| upx -9 (UCL, what upstream used) | 282,556 | 177,408 |
| upx --best --lzma | 244,476 | 155,280 |
| upx --ultra-brute (--lzma) | 244,328 | 155,160 |
| xz --x86 -9e (payload only, no loader) | 241,762 | 151,389 |
| **p1cm stub (ours)** | **222,312** | |

**p1cm** (`p1cm.h`, `pack.c`, `stub.c`, `build_stub.sh`): a small bitwise context-mixing packer for x86 code: E8/E9
call/jmp transform, 15 hashed contexts (orders 1-6 plus sparse/alignment contexts tuned on the C3 exe) with
count-adaptive probabilities, a match model, two mixers (selected by bit position + match state, and by the
previous byte), an order-1 APM. Integer-only, so packer and stub compute identical probabilities on any CPU.
The stub is freestanding (no libc, raw syscalls, 3.7 KB with headers, no RWX segment): it decodes the embedded
payload, writes it to a `memfd_create` file and `execveat`s it with the original argv/envp (fallback: `./.p1x` +
execve). The real exe sees its own name in argv[0] and still opens itself as `./cmix` / `./archive9` from the cwd,
so the self-extractor works unchanged. Cost: about 2.9 s and ~500 MB of transient RSS per start (comp9 starts 3
times, archive9 twice), nothing after exec. Deterministic: two builds are byte-identical.
p1cm beats UPX's best by 22,016 B per copy on C3 (-44 KB of S) and by 18,880 B on the final exe below.

## 3. Code size (archive must stay bit-identical)

| change | exe raw | p1cm payload | notes |
|---|---:|---:|---|
| C3 | 604,536 | 218,574 | |
| E12 without std::regex (`e12_re.h`, `0001`) | 533,792 | 196,572 (−22,002) | tiny DFS matcher with ECMAScript priority; differential test vs std::regex: 0 mismatches (see below) |
| + no unwind tables in the CM objects (`-fno-asynchronous-unwind-tables -fno-unwind-tables`) | 513,312 | 190,484 (−6,088) | the CM code is -fno-exceptions; normalized objdump with/without is identical (only addresses move) |
| + bit_head at -O2 + FX2TFWC3 decoder (`0002`) | 501,024 | 185,818 (−4,666) | bit_head is value-safe FP (no fast-math, contract off), so -O level can't change its results |
| + rank-delta order reader (`0004`) | 501,024 | 186,278 (+460) | pays for −19,361 B of order stream |
| E12/X2 at -Os via pragma (`0003`, off) | | +40 | no-go, attic/2026-11-07-P1-e12-x2-Os |

`e12_re_difftest.cpp` compares every E12 pattern against std::regex at every trigger point of the text
(tails of 400 B, percent lists, u18, number tokens, article windows): 300 KB census text: 1,172,561 tests, 36,685
matches, 0 mismatches; enwik9_mid10M_pa + enwik9_10M: 73,268,944 tests, 1,276,432 matches, 0 mismatches.

**Biggest remaining code lever (not done, changes the profile):** `-fprofile-partial-training` keeps every function
the PGO training run never executed at -O3. Without it the final exe is 439,584 B raw / **164,441 B packed
(−21,837 per copy, about −44 KB of S)**. But the training run (`prof_input`, CM only) never runs the transformer
glue, so that would be compiled for size, and a different profile can change fast-math code generation, i.e. the
archive. Doing it needs a training run that covers the full-mode hot paths, then a speed and bit-exactness check
(the archive would likely change, harmlessly). Out of P1's "archive bit-identical" scope.

## 4. Weights, dict and order streams

- **Weights (FX2TFWC2, 2,930,652 B).** Measured costs with upstream's model: int4 weights 2,803,285 B (5,868,864
  weights, 3.82 bits each), f32 97,307 (28,313 floats, mantissas are random: 81.4 KB of raw mantissa bits),
  bf16 23,373, names/metadata ~6.7 KB. The int4 weights are close to i.i.d.: static order-0 is 2,777,439 B;
  per-row/column/previous-weight contexts gain at most 0.06% adaptively (the static per-row "gains" were model
  cost). The loss was the coder's fast adaptation (11-bit probabilities, shift 5). **FX2TFWC3** = v2 with the int4
  tree at 16-bit probabilities, shift 9: **2,902,850 B (−27,802 per copy, −55,604 of S)**, decodes to the same
  tensors (`mk_tfwc3.py` checks that, and that the v2 writer still reproduces the original file byte for byte).
  The C++ loader accepts both magics (`0002`). Count-based adaptation would gain ~700 B more; not worth the code.
- **Dict (99,889 B).** english.dic is in a semantic order (its order is the WRT code assignment), so it can't be
  sorted or front-coded; cmix already codes it at 1.94 bits/byte. Left as is.
- **Order (200,493 B, counted once).** A permutation of 172,277 article ids (log2 N! = 343,514 B, so the CM
  already finds a lot of structure). Re-coded as **rank deltas among the still-unused ids** (`order_rankdelta.py`,
  decoder `order_rankdelta.h`, reader patch `0004`), the same cmix codes it to **181,132 B (−19,361)**.

## 5. Totals

| | exe | dict | order | weights | comp9 | S delta vs C3 unpacked | vs C3 + upx --ultra-brute |
|---|---:|---:|---:|---:|---:|---:|---:|
| C3 unpacked | 604,536 | 99,889 | 200,493 | 2,930,652 | 3,835,586 | 0 | |
| C3 + upx --ultra-brute --lzma | 244,328 | 99,889 | 200,493 | 2,930,652 | 3,475,378 | −720,416 | 0 |
| **P1 final** | **190,024** | 99,889 | 181,132 | 2,902,850 | **3,373,911** | **−903,989** | **−183,573** |

S delta = 2×(Δexe + Δdict + Δweights) + Δorder (comp9 and archive9). P1's comp9 is 54,563 B smaller than the official
record's comp9 (3,428,474) even though it carries all of C3. [Bench results below.]

## Files

- `p1cm.h`, `pack.c`, `stub.c`, `build_stub.sh <in-exe> <out-exe>`: the packer and stub.
- `e12_re.h`, `e12_re_difftest.cpp`, `0001-P1-e12-no-regex.patch`: E12 without std::regex.
- `0002-P1-tfwc3-weights.patch`, `mk_tfwc3.py <in.tfwc2> <out.tfwc3> <patched-tree>`: FX2TFWC3.
- `0003-P1-e12-x2-Os.patch`: attic'd (-Os pragma), applied only with `P1_OS=1`.
- `order_rankdelta.py`, `order_rankdelta.h`, `0004-P1-order-rankdelta.patch`: rank-delta article order.
- `build_c3re.sh`: C3 + 0001 only (`build/exp/P1/c3re`). `build_p1.sh`: C3 + all of the above
  (`build/exp/P1/c3p1/bin/cmix`, plus `c3p1/new_article_order.rd`, the order file to CM-code into comp9).
- `run.sh <bin> <name> <comp9-bytes> <notes>`: fast-lane 1M bench with C3's env (`SLICE=`, `FX2_WEIGHTS=` pass through).
- `perf_cmp.sh`: instruction counts of slice-mode compression on two 200 KB slices.

To assemble the final comp9: `build_p1.sh`; `mk_tfwc3.py` → weights; `build_stub.sh c3p1/bin/cmix cmix_packed`;
then upstream's construct step with the packed exe, `new_article_order.rd` as the order file and the .tfwc3 file.
Whoever bakes the full-mode build (C3-full) has to carry these patches; the order change needs a full -e/-d check
there (slice mode never reads the order file).
