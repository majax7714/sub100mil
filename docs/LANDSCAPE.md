# Landscape: enwik9 compressors, the lineage to the record, and directions to ~95M

Survey date: **2026-11-04**. This is read-only research: no third-party code was cloned or run. Every factual
claim has a source tag (listed at the end). **UNVERIFIED** marks anything I could not confirm from a primary
source. **ESTIMATE** marks my own arithmetic or projections.

Constraint background (S accounting, time, RAM, AVX2) lives in [`RULES.md`](RULES.md) and is not repeated here.

---

## 0. TL;DR

- **Official record:** L = **100,424,672** (fx2-cmix-transformer, Vladimir Ivanov, 24 Jul 2026, awarded 30 Sep 2026) [HP][LTCB].
- **Pending entries already beat 100M** [LTCB]:
  - zmix 1.0: 99,312,424
  - cmix-lex-transformer lexth11c: 99,313,750
  - **cmix-neif 1.0: 95,914,051**

  ex-girlfriend 1.0 is a non-eligible LTCB-only derivative of neif at 95,773,359.
- **The one architectural jump in the lineage was fx2-cmix to fx2-cmix-transformer: −10.37 MB of S.** It replaced
  the online 0.4M-parameter LSTM with a 6M-parameter transformer pre-trained offline on enwik9 itself. The weights
  are 4-bit and count twice in S [FX2T][LTCB].
  - Every other step in 2019–2026 bought about 1.0–1.8 MB through tuning, preprocessing and model additions.
  - cmix-neif bought −4.51 MB over the record by stacking a bigger transformer, *prompt injection* (feeding the
    transformer context the decoder can derive for free), a restructured stream, and most of the other 2026 ideas
    [NEIF].
- **Where the bits go:** about **90% of cmix-neif's 87.9 MB payload is ordinary article text**, coded at about 1.43
  bits per stream byte. The metadata block, redirects, census pages and tail classes together are under 10%
  [NEIFRATE, my aggregation]. Nothing finer (prose vs. links vs. templates vs. tables) is published.
- **The binding constraint is time, not weight bytes.** cmix-neif runs 48.7 h against a 53.6 h limit [NEIF]. Its
  transformer takes about 44–48% of the run time [NEIF][LEXTW].
- **Weights cost about 0.95 bytes of S per parameter** at the current about 3.8 bits per weight, because they are
  counted twice (ESTIMATE from [NEIF]). The evidence suggests the size/payload frontier is near 8M parameters for
  current training recipes. So the next gains have to come from:
  - context (more free conditioning information),
  - compute per weight byte (layer reuse, wider windows, paid for by speedups),
  - better bits per weight,
  - not from simply scaling parameters.
- **Recommended base: cmix-neif-1.0** (GPL-3.0-only), at
  `marwoelk/cmix-neif-1.0@1c9ea5ed57ec9718bcb681d20b79e81a60b7a728`. Use fx2-cmix-transformer-v1 as the clean
  ablation base and zmix as the reference for mechanisms (§6).

---

## 1. Lineage table

### 1.1 How to read mattmahoney.net/dc/text.html (the earlier confusion)

The LTCB main table [LTCB] has these columns:

`Program | Options | enwik8 | enwik9 | Decompresser size (zip) | Total size enwik9+prog | Comp ns/byte | Decomp ns/byte | Mem MB | Alg | Note`

- For **self-extracting Hutter-style entries**, the `enwik9` column is the size of **`archive9`, which already
  contains the decompressor**. The decompresser column is `0 xd`, and the **"Total" column equals `archive9`
  only.** LTCB **does not count the compressor.** Its notes say so: "The compressor size is shown because it is
  used for scoring in the Hutter prize, although not in this benchmark" [LTCB #0969].
- **Hutter S = compressor (`comp9`) + `archive9` (+ option bytes).** You get S from the per-entry **notes**
  tables (columns `archive9 | C size | Total`), not from the main table.
- **Status is not a column.** It is stated in prose in each entry's note: "Hutter prize entry (awarded Sept. 30,
  2026)", "Hutter prize submission (pending)", "(not eligible)", "formatted entry (not eligible)", or "not a
  Hutter prize candidate". The only official record table is on [HP].
- Times are ns per byte of enwik9 (so 180,000 ns/byte = 50 h), on whichever machine the note number says. They
  are **not normalized**.

Exact LTCB main-table rows relied on ([LTCB], "Last update: Oct. 14, 2026"; HTML stripped, spacing as in the
source):

```
Program           Options                       enwik8      enwik9     size (zip)   enwik9+prog  Comp Decomp   Mem Alg Note
ex-girlfriend 1.0                                          91,778,237          0 xd  91,778,237 220071 241636 10204 Tr  124
cmix-neif 1.0                                              91,913,343          0 xd  91,913,343 175392 175428  9750 Tr  119
makimono 1.0                                               95,793,872          0 xd  95,793,872 228540 220740  9970 Tr  123
cmix-lex-transformer lexth11c                              95,836,613          0 xd  95,836,613 199440 213840  9765 Tr  117
zmix 1.0                                                   96,096,261          0 xd  96,096,261 131364 131184  9370 Tr  115
rata-cmix 1.0.0                                            96,849,689          0 xd  96,849,689 190633 187200  9537 Tr  113
fx2-cmix-t-ft8                                             96,945,005          0 xd  96,945,005 111600 129960  9681 Tr  121
fx2-cmix-transformer                                       96,996,198          0 xd  96,996,198 133766 131561  9547 Tr  108
fractal-zip                                   12,303,845   97,617,144          0 xd  97,617,144 151632 159840  9999 Tr  118
nncp v3.2                                     14,915,298  106,632,363    628,955 xd 107,261,318 241871 238670  7600 Tr  88
cmix-obias 18 Aug 2026                                    108,009,834          0 xd 108,009,834        119920  9836 LSTM  108
cmix v21          -t                          14,623,723  107,963,380    281,387 sd 108,244,767 622949 638442 30950 CM  83
cmix-lex                                      14,952,093  109,190,109          0 xd 109,190,109        265512  9407 LSTM  106
fx3-cmix                                                  109,190,129          0 xd 109,190,129        272644  9686 LSTM 102
cmix-hp 10 Jun 2021                           15,957,339  113,712,798          0 xd 113,712,798 189420 194280  6873 CM  89
fast-cmix                                                 113,746,218          0 xd 113,746,218        121971  8027 CM  99
starlit 31 May 2021                           15,215,107  114,951,433          0 xd 114,951,433 173953 171682 10233 CM  89
phda9 1.8                                     15,010,414  116,544,849     42,944 xd 116,587,793  86182  86305  6319 CM  83
```

Exact LTCB note rows for the Hutter split ([LTCB] #0919, #0958, #0960, #0969):

```
Version         Date              archive9    C size        Total       C time  D time   C mem  D mem  Note
cmix-neif-pre   Sep 15 2026       96,021,006  3,104,729     99,125,735
cmix-neif-pre2  Sep 16 2026       95,135,738  3,114,097     98,249,835          118.55h         9989   106
cmix-neif-pre3  Sep 20 2026       94,128,136  3,133,539     97,261,675          107.73h        10030   106
cmix-neif-pre3f Sep 28 2026       94,128,758  3,134,186     97,262,944          124.15h         9730   106
cmix-neif 1.0   Oct  5 2026       91,913,343  4,000,689+19  95,914,051  48.72h   48.73h  9750   9750   119
                                                                                 82.87h        10185   106
                                                                        48.99h   49.16h  9749   9750   122
cmix-lex-transformer lexth11c    95,836,613   3,477,137   99,313,750  55.4h   59.4h   9765   9597 MB  117
                                                                              49.3h          9596 MB  106
zmix 1.0           96,096,261   3,216,163  99,312,424  131364  9782   131184  9370   115
                                                                      318947  9208   106
24 Jul 2026  96,996,198  3,428,474  100,424,672  133766  131561   9663   9547  108 (AMD)
21 Aug 2026  96,994,188  3,426,642  100,420,830          695558          8418  106 (Intel)
```

Exact official record rows ([HP], "Current and Previous Records"; HTML stripped):

```
Vladimir Ivanov            24.Jul 2026  fx2-cmix-T   100'424'672  9.96 | ~10GB | ~50h   7.46%| 37'300€
David Freelan              19.Jul 2026  cmix-obias   108'521'870  9.21 | ~10GB | ~50h   1.05%| 5'240€
Ibrahim Marcouch & K. Orav 26.Jun 2026  cmix-lex     109'671'639  9.11 | ~10GB | ~50h   1.01%| 5'050€
Kaido Orav & Byron Knoll    3.Sep 2024  fx2-cmix     110'793'128  9.03 | 95% | 99%      1.59%| 7'950€
Kaido Orav                  2.Feb 2024  fx-cmix      112'578'322  8.88 | 8.9GB | ~50h   1.38%| 6'911€
Saurabh Kumar              16.Jul 2023  fast cmix    114'156'155  8.76 | 8.4GB | 43h    1.04%| 5'187€
Artemiy Margaritov         31.May 2021  starlit ...  115'352'938  8.67 | 10GB | ~50h    1.1% | 9000€
Alexander Rhatushnyak       4.Jul 2019  phda9v1.8 .. 116'673'681  8.58 | 6.3GB | ~23h   -- | pre-prize
```

### 1.2 The table

S is the official Hutter total where one exists ([HP] for awarded entries, [LTCB] notes for the others).
The split columns are `archive9` (which contains the decompressor) and `comp9`. Times are as published, on the
machine named, and **not normalized**.

| Entry | Total S | archive9 / comp9 | Date | Author | Status | Source / license | enwik9 time; RAM | Neural model |
|---|---:|---|---|---|---|---|---|---|
| phda9 1.8 | 116,673,681 [HP] | LTCB: 116,544,849 + 42,944 decompr. = 116,587,793. The 85,888 B gap to [HP] is **UNVERIFIED** | 4 Jul 2019 | Alexander Rhatushnyak | pre-prize baseline | Binaries only. The enwik transforms were later open-sourced via starlit [FAQ][STARLIT]. License of 1.8: **UNVERIFIED** | 86,182 / 86,305 ns/B (~24 h), 6,319 MB, i7-7700K [LTCB] | None (CM) |
| starlit | 115,352,938 [HP] | 114,951,433 / 401,505 [LTCB #1149] | 31 May 2021 | Artemiy Margaritov | awarded | github.com/amargaritov/starlit, GPL-3.0 [GHAPI] | 173,953 / 171,682 ns/B (~48 h) on i7-1165G7, ~10 GiB [LTCB] | LSTM (cmix, 1×200 cells) [STARLIT] |
| cmix-hp v3 (unawarded step) | 114,130,141 (own measurement) [FASTCMIX] | 113,733,212 / 396,929 [FASTCMIX] | Aug 2021 | Byron Knoll | interim | cmix-hp mirror, GPL [FASTCMIX] | 42.8 h, 6.9 GB [FASTCMIX] | LSTM |
| fast cmix | 114,156,155 [HP] | LTCB archive 113,746,218. comp ≈ 409,937 (derived, **UNVERIFIED**) | 16 Jul 2023 | Saurabh Kumar | awarded | github.com/saurabhk/fast-cmix, GPL-3.0 [GHAPI] | D 121,971 ns/B (33.9 h) on 3700X, 8,027 MB [LTCB] | LSTM |
| fx-cmix | 112,578,322 [HP] | LTCB archive 112,142,259. The README says 112,148,343 + 436,707 = 112,585,050 [FX]. **The gap is unexplained** | 2 Feb 2024 (submitted Dec 2023) | Kaido Orav | awarded | github.com/kaitz/fx-cmix, GPL-3.0 | 85 h on GB5 706 box, 9.1 GiB [FX] | LSTM (cmix) |
| fx2-cmix | 110,793,128 [HP] | 110,351,665 / 441,463 [FX2]. Payload 110,111,245 [FX2] | 3 Sep 2024 (subm. 11 Aug) | Kaido Orav & Byron Knoll | awarded | github.com/kaitz/fx2-cmix, GPL-3.0 | 65 h on GB5 1026 box, 9.1 GiB [FX2] | LSTM, about 0.4M params, online [FX2T] |
| cmix-lex | 109,671,639 [HP] | 109,201,040 / 470,599 (AMD rebuild by Bowery) [LTCB #1091]. Payload 108,929,925 [LEX] | 26 Jun 2026 | Ibrahim Marcouch (& K. Orav per [HP]) | awarded | github.com/blahem/cmix-lex, GPL-3.0 | 40.3 h C / 40.8 h D on 5900X (T=1648), 9,993 MB [LTCB] | LSTM, 170 cells [NEIF] |
| cmix-obias | 108,521,870 [HP] | Submission doc: 108,009,834 + 459,989 + 23,002 B head = 108,492,825 [OBIAS]. **The 29,045 B gap to [HP] is UNVERIFIED** (likely an AMD rebuild; see the cmix-lex precedent in [OBIAS]) | 19 Jul 2026 | David Freelan | awarded | huggingface.co/dfreelan/cmix-obias, GPL-3.0 | D 119,920 ns/B (33.3 h) on 5900X, 9,836 MB [LTCB] | LSTM 256 cells + offline-trained "bitlstm32" correction head [OBIAS] |
| **fx2-cmix-transformer** (record) | **100,424,672** [HP] | 96,996,198 / 3,428,474 [LTCB #0969] | 24 Jul 2026 | Vladimir Ivanov | **awarded, current L** | github.com/astOwOlfo/fx2-cmix-transformer-v1, GPL-3.0 | 133,766 / 131,561 ns/B (37.2 / 36.5 h) on AMD, 9,663 / 9,547 MB. 695,558 ns/B (8 days) on the Intel WSL2 box [LTCB] | **Transformer, 6M params, pre-trained offline, frozen**. int4 weights / int8 activations, 2.9 MB per binary [FX2T] |
| fx2-cmix-t-ft8 | 100,367,271 [LTCB] | 96,945,005 / 3,422,266 | 10 Oct 2026 | J. Mejuto González | not eligible | github.com/mejutoco/fx2-cmix-T-ft8 (license **UNVERIFIED**) | 31.0 / 36.1 h on 5900X [LTCB] | Same model plus 8 more epochs ("about $20") [LTCB] |
| rata-cmix 1.0.0 | 100,219,121 (ESTIMATE, sum) | 96,849,689 / 3,369,432 [LTCB #0968] | 12 Sep 2026 | D. Patel, N. Bijalwan | not a HP candidate | github.com/Dharmesh2015/RATA-CMIX (license **UNVERIFIED**) | 190,633 / 187,200 ns/B, 9,537 MB [LTCB] | Frozen transformer + 2×200 online LSTM [LTCB][RATA] |
| zmix 1.0 | 99,312,424 [LTCB][ZMIX] | 96,096,261 / 3,216,163 | 14 Sep 2026 | James Byrne (with Claude) | **pending**, "still undergoing testing" [LTCB] | zmix.frostbyrne.io/v1.0/ (source tarball), GPL-3.0-or-later [ZMIX] | 36.49 h C on 5900X (T=1724), 9.78 GB. D 318,947 ns/B on the Intel box [ZMIX][LTCB] | Transformer (fx2-T architecture, retrained with a weight-size term) [ZMIX] |
| cmix-lex-transformer lexth11c | 99,313,750 [LTCB][LEXT] | 95,836,613 / 3,477,137 | 27 Sep 2026 | Neel Patel (with Codex) | **pending** | github.com/Neel49/cmix-lex-transformer, GPL-3.0 | 55.4 h C / 59.4 h D on cloud Ice Lake (T not stated), 9.77 / 9.60 GB. 49.3 h D on the Intel box [LTCB][LEXT] | Transformer, 12 layers, retrained 1,280 epochs, codebook int4, 2,978,039 B container [LEXT] |
| **cmix-neif 1.0** | **95,914,051** [LTCB][NEIF] | 91,913,343 / 4,000,689 + 19 B options. Payload ≈ 87.9 MB [NEIFRATE][EXGF] | 5 Oct 2026 | Marcus Wölk (with Claude Code) | **pending** [LTCB] | github.com/marwoelk/cmix-neif-1.0, **GPL-3.0-only** [NEIF] | 48.72 / 48.73 h on EPYC 7B13 (T=1306, limit 53.6 h), 9.75 GiB. **82.87 h D, 10,185 MB on the Intel box** [LTCB][NEIF] | Transformer, 16 layers, about 7.8M params, 3,716,667 B container ×2 + 80-cell online LSTM + 16-cell BITLSTM [NEIF] |
| makimono 1.0 | 99,778,850 (sum). LTCB prints 99,788,850, **likely a typo** | 95,793,872 / 3,984,978 [LTCB][MAKI] | 13 Oct 2026 | A. Sklyarenko | not eligible | github.com/krapuchino83/makimono, GPL-3.0-or-later [MAKI] | 63.5 / 61.3 h on shared EPYC 9755 VM [MAKI] | zmix + 5/7-bit retrained weights, 3,579,747 B container [MAKI] |
| ex-girlfriend 1.0 | 95,773,359 [LTCB] | 91,778,237 / 3,995,122 | 14 Oct 2026 | Ryan Landay (with Claude, Codex) | "formatted entry (not eligible)" [LTCB] | github.com/rlanday/ex-girlfriend, GPL-3.0 | 61.1 / 67.1 h on a shared Threadripper 3970X, 9.97 GiB [EXGF] | neif + online fast-weight head on the last 4 MLP blocks [EXGF] |
| fx3-cmix | archive 109,190,129 [LTCB] | n/a | 5 Oct 2026 | K. Orav, Z. Gotthardt | not eligible | github.com/kaitz/fx3-cmix | D 272,644 ns/B [LTCB] | LSTM, about 725 fxcm predictions [FX3] |
| fx-deepmix | archive 107,828,411, comp 458,116 [LTCB] | n/a | 28 Jul 2026 | Halvor Yttredal | not eligible | source zip on LTCB | D 301,180 ns/B, 10,233 MB [LTCB] | 2×200 LSTM, int8 kernels, AVX-512 [LTCB] |
| **nncp v3.2** (context) | 107,261,318 LTCB total (archive 106,632,363 + 628,955 decompr.) | n/a (not an SFX) | Oct 2023 | Fabrice Bellard | LTCB only (needs a GPU) | bellard.org/nncp (license **UNVERIFIED**) | 241,871 ns/B on an **RTX 3090 GPU**, 7.6 GB [LTCB] | **Online-trained transformer**, "similar (199M parameters)" per LTCB for v3 [LTCB][NNCP] |
| cmix v21 (context) | 108,244,767 LTCB total | 107,963,380 + 281,387 | Sep 2024 | Byron Knoll | LTCB only | GPL | 622,949 ns/B, 30,950 MB [LTCB] | LSTM + CM |
| ts_zip (context) | 135,443,237 (enwik9, compressed only) [TSZIP] | n/a | n/a | Fabrice Bellard | not eligible (external LLM, GPU) | bellard.org/ts_zip | Up to 1 MB/s on an RTX 4090 [TSZIP] | RWKV 169M, 8-bit, pre-trained on external data [TSZIP] |
| altxs, tufazip (context) | 106,924,811 and 105,388,353 LTCB totals | n/a | Aug–Sep 2026 | various | LTCB only, GPU required | MIT(+GPL) [LTCB] | GPU, 63–330 h [LTCB] | Transformers (GPU) |

**Interpreting pending status** (from [RULES] §5.2, unchanged by this survey):
- Submissions are processed in order received.
- If zmix (14 Sep) is verified, L ≈ 99.31M.
- lexth11c would then not clear 1%.
- If cmix-neif (5 Oct) is then verified, L ≈ 95.91M and the prize bar becomes about **94.95M**.

---

## 2. Per-entry delta (what changed, how many bytes, what kind)

Classes:
- **T** = tuning
- **P** = preprocessing
- **M** = model addition (new contexts, SSE/APM, mixer changes)
- **A** = architectural (new predictor class)
- **E** = engineering/packaging (speed, memory, binary size). E has no direct rate effect but buys time or bytes.

Deltas are in official S where possible. Few authors publish per-item numbers, so most steps can only be
attributed as a bundle.

| Step | ΔS | What changed (sources) | Class |
|---|---:|---|---|
| phda9 1.8 → starlit | −1,320,743 | Switched base from phda9 to a reduced cmix v18 (adds cmix's **LSTM mixer**, 1×200 cells, plus the PPMd model). Article reordering by similarity. phda9 transforms ported and hardened. PAQ8 model and layer-1 mixers disabled to fit 10 GB [STARLIT][LTCB #1149] | **A** (new predictor class relative to phda9) + **P** (reordering) − model removal |
| starlit → cmix-hp → fast cmix | −1,196,783 | cmix-hp: one huge order-25 PPMd mmapped to disk, "swapped certain byte regions" (a PAQ8HP trick), PAQ8HP zero-state and UTF tweaks [FASTCMIX]. fast-cmix: pure speed work (devirtualization, cache locality, faster hash maps) [FASTCMIX]. Most of the rate gain is cmix-hp's | **M** (big PPM) + **P** + **T**; fast-cmix itself **E** |
| fast cmix → fx-cmix | −1,577,833 | paq8hp model replaced by **fxcmv1**: about 30 predictors, wiki-aware contexts (links, tables, templates, brackets, quotes, columns), match model, multiple state tables. New dictionary [FX] | **M** (large CM rewrite) + **P** |
| fx-cmix → fx2-cmix | −1,785,194 | Stemmer-based NLP word streams and word types, sparse match for escaped UTF-8, more parsing, mixer/APM context changes (M). Online reverse-dictionary transform, single-pass wiki transform, **new article order from voyage-large-2 embeddings, t-SNE to 1-D and k-means** (P). Mixer weight-update skipping, model removal (E/T) [FX2] | **M** + **P** |
| fx2-cmix → cmix-lex | −1,121,489 | fxcm_v26 integration (M). `payload_lex`: reorders the 45 MB structured PHDA9 metadata tail, with a 679,489 B raw side blob (P). Updated article order (P). Disk-backed PPM improvements (E) [LEX][LEXCH] | **M** + **P** |
| cmix-lex → cmix-obias | −1,149,769 | About +76% speed (15 optimizations), spent on a **256-cell byte LSTM** (T/scale). **bitlstm32**: a 23,002 B offline-trained fp16 LSTM that corrects the final bit probability (M). PPMd log-prior added to the LSTM output logits, g=0.15, "+1,821 / +2,771 / +4,837 B" saved at 10/25/50 MB (M) [OBIAS] | **T** + **M** (first offline-trained weights in the lineage) |
| **fx2-cmix → fx2-cmix-transformer** | **−10,368,456** (archive −13,355,467, comp +2,987,006) | Online LSTM (0.4M params, 1.23 nats/byte) **replaced by a 6M-param transformer trained offline on enwik9**: 320 + 256 QAT + 16 epochs on 8× RTX 5090 for 26 h, ending at 0.911 nats/byte. 75% Kimi-Linear (KDA) layers, sliding window 1024, Muon, RoPE, QK-norm, ReLU². ParetoQ-style QAT to int4 weights (stored at log2(15) ≈ 3.81 bits) and int8 activations. AVX2 `VPMADDUBSW` kernels. PPMd distribution fed as an input. Three bug fixes. No other changes to fx2-cmix [FX2T]. Note the base is fx2-cmix, **not** cmix-lex/obias. Against obias the delta is −8,097,198 | **A** |
| fx2-cmix-T → fx2-cmix-t-ft8 | −57,401 | 8 more training epochs [LTCB] | **T** (cheap: "about $20") |
| fx2-cmix-T → zmix | −1,112,248 (archive −899,937, comp −212,311) | Weights retrained with **a loss term for the compressed size of the weights**, plus a tighter container (T). Model stack ported to Zig with dead paths removed at compile time, giving a smaller decompressor (E). "Table layouts, state representations and the quantization of the context-model bank are changed, and the mixer and the SSE readout stage are reworked" (M/T). Adaptive depth, end-to-end mixer gradient, word-label log-bilinear model, indel-recovering match (M, per [NEIF] §1) [ZMIX] | **T** + **M** + **E** |
| fx2-cmix-T → lexth11c | −1,110,922 | 3-way merge: cmix-lex CM + fx2-T transformer (M). Transformer retrained from scratch for 1,280 epochs with a Lloyd–Max codebook int4 (T). PPMd pointer bug fixed, then pruned to a 3,500 MB in-RAM heap: "−0.30% with a 1,300 MB heap vs 14 GB" (M/E). 12 of 23 layer-0 mixers kept, LR scales (T/E) [LEXT][LEXTW] | **M** + **T** |
| zmix → cmix-neif 1.0 | −3,398,373 (vs record −4,510,621; archive −5,082,855, comp +572,215) | See the breakdown below. Most items are *measured on prefixes and are sub-additive* [NEIF] §3 | **A** (bigger transformer + prompt injection) + **P** + **M** + **E** |
| cmix-neif → ex-girlfriend | −140,692 (archive −135,106, comp −5,567) | Online **fast-weight** correction from the last 4 MLP blocks' activations into a second output head, both fed to the mixers (A-lite). Bigger tables (T). Page-id-based revision-id forecast and timestamp slope (P). Payload 87,896,203 → 87,764,097 [EXGF] | **A** (online adaptation) + **T** + **P** |
| zmix → makimono | **+466,426** (archive −302,389, comp +768,815) | Higher-precision 5/7-bit retrained weights distilled from fx2-T's fp model, plus 28 models re-enabled [MAKI][LTCB]. The **archive got smaller but S got larger**: about 1.07 MB of payload saved for about 1.54 MB of extra weight bytes counted twice (ESTIMATE). This is the clearest public data point on the double-count tax | **T** (loses on S) |

**cmix-neif 1.0 in more detail.** These are the author's own figures [NEIF] §3–4. They are approximate and partly
extrapolated, and the author warns they do not add up.

- **Byte model (A):**
  - Transformer grown to 16 layers (12 KDA + 4 SWA), d=192, MLP 768, about 7.8M params, window 2048.
  - Trained from scratch on neif's own stream: 1,000 bf16 epochs + 32 QAT + 6 fp.
  - "About −6 MB" on the archive from a first, smaller proof-of-concept transformer replacing the LSTM.
  - Window 2048 vs 1024: −48.6 KB at 39% of the stream. 2560 would add about −13 KB but costs +5.5% instructions.
- **Prompt injection (A):**
  - Free rows fed to the transformer: page number, id, contributor, date, redirect synonyms, link context from
    earlier pages, aliases, heading reminders, digit-run classes.
  - Not isolated. A bug that dropped the rows cost 167 KB at 20%, about 0.8–0.9 MB extrapolated. That shows how much
    the trained model relies on them, not what they gain.
- **Stream (P):**
  - Page form without XML: −17.5 MB of stream, a time gain.
  - Metadata block moved to the head, split and sorted: −62 KB.
  - Timestamp forecast: −45 KB on the block.
  - Census transform: −64 KB and −29 KB in two steps.
  - Larger dictionary: −106 KB, then −19 KB.
  - Dictionary derived from hints: −50 KB of S.
  - Order shipped as rule plus residual: −100 KB of S.
  - `&sup2;` mapped to a control byte: −41 KB.
  - Redirects moved to the front: **+90 KB**, kept because the prompts depend on it.
- **Model and mixer (M):**
  - Second match channel from fx-deepmix: −26 KB.
  - Direct-state-map widths: −39 KB.
  - Mixer learning rates: −33 KB and −46 KB.
  - BITLSTM vs none: about −0.3 MB (estimate).
  - PPMd per-context order limit: −20 KB.
  - ISBN check digit: −10 to −13 KB.
  - Number and date contexts: about −60 KB (estimate).
- **Packaging (E):**
  - Own CM-based x86 SFX stub: −101 KB of S vs the earlier stub.
  - Container format v3 and v4: −115,514 B per copy, then −10,301 B.
  - Dead neurons removed: −21,922 B of S.
  - Size-optimized compile: −36 KB.
  - Encoder-only code carried once: −22 KB.
- **Progression:** neif-pre 99.13M → pre2 98.25M → pre3 97.26M → 1.0 95.91M in 3 weeks [LTCB #0919]. The 1.0 step
  grew `comp9` by +866,503 B (the larger model) and cut the archive by −2.215 MB.

**Summary of the lineage by class.**
- From 2019 to mid-2026, each prize step bought 1.0–1.8 MB, always the sum of many P/M/T items.
- Article reordering, the fxcm model family and dictionary/wiki transforms account for most of 116.7 → 108.5M.
- **The single A step (pre-trained transformer) bought 8–10 MB at once.** neif's second-order A items (scale plus
  prompt injection) bought roughly another 3–4 MB together with many P/M items.
- No public number isolates the A share inside neif.

---

## 3. Where the bits go

### 3.1 What is published

1. **No published breakdown of enwik9 by content type** (prose, links, templates, tables, refs, metadata,
   non-English) for any top compressor. A web search turned up none [SEARCH]. LTCB's "About the test data" gives
   only structure and a PPMd incremental-rate curve [TD]. Other basic facts in [TD]:
   - 243,426 titles, of which 85,560 are redirects.
   - Census-generated US town articles form a highly compressible "dip in the middle" of the original order.
   - Long-range repeats across the whole file, with frequent 4–8-byte matches about 3,000 bytes (one article) apart.
2. **cmix-neif publishes a per-window rate trace** of its coder stream, in windows of 0.1%: `doc/stream_rate.tsv`
   [NEIFRATE]. The stream is 552,750,198 B and the coded output 87,897,431 B (that run used a different PGO
   profile). The README states [NEIF] §4.5:
   - mean 1.27 bits per stream byte;
   - "page ids and revision ids in part A of the metadata block are the most expensive bytes";
   - census pages are cheapest at 0.43;
   - the calendar block is 0.74, against articles at 1.52 before it and 1.31 after.

   Aggregating that TSV by regions (boundaries placed by me at rate change points, so the region labels are
   ESTIMATES) gives:

| Region of neif's coder stream (approx.) | Stream bytes | Coded bytes | Share of payload | bits / stream byte |
|---|---:|---:|---:|---:|
| 0–0.9%: metadata block (ids, timestamps, contributors) | 4.97 M | 1,346,124 | 1.5% | 2.17 |
| 0.9–2.0%: redirect block | 6.08 M | 742,115 | 0.8% | 0.98 |
| 2.0–82.5%: ordinary articles (incl. the calendar block) | 444.97 M | 79,405,262 | **90.3%** | 1.43 |
| 82.5–95.5%: census pages | 71.86 M | 3,584,078 | 4.1% | 0.40 |
| 95.5–100%: image, `Wikipedia:` and disambiguation pages | 24.86 M | 2,819,852 | 3.2% | 0.91 |

3. **cmix-lex** documents that its post-WRT stream (586 MB) ends in a 45.3 MB structured PHDA9 metadata tail with
   three regimes [LEXCH]. neif later moved this block to the head and re-encoded it [NEIF] §4.9.
4. ex-girlfriend: its metadata forecasts (E1+E3b) saved 22.8 KB on a matched 12 MB screen [EXGF]. This suggests the
   structured parts are still not saturated.

**Implication.** About 90% of the remaining payload is article text coded at about 1.43 bits per stream byte. A
4–5% improvement in the article-text model is worth 3.2–4.0 MB. All of the non-article regions together are worth
about 8.5 MB, so a heroic 20% gain on all of them would buy only about 1.7 MB. **The path to ~95M runs through the
article-text predictor**, which in practice means the transformer and its mixing.

### 3.2 How we would measure it ourselves (proposal)

1. **Per-byte cost logging.** Add a hook in the coder: for every coded bit add −log2 p(bit) to the current stream
   byte, then dump a per-byte cost array (float16 is enough). neif already computes per-window rates, so the hook is
   a few lines in its encoder loop.
2. **Class tagging.** Run a small state machine over the coder stream (or over raw XML when we test our own coders
   on raw slices). Tag each byte with:
   - metadata fields and comment line;
   - redirect page;
   - prose;
   - `[[link]]` target vs. anchor;
   - `{{template}}`/infobox;
   - `{| table |}`;
   - `<ref>`/citation;
   - URL;
   - heading;
   - list item;
   - category or interlanguage link;
   - number/date;
   - non-ASCII UTF-8;
   - HTML entity.

   For neif's stream, the dictionary codes (bytes ≥ 0x80) map back to words, so prose vs. markup can be tagged on
   the transformed stream directly.
3. **Report** bytes, coded bytes and bits/byte per class for at least two codecs: our baseline and neif run on
   windows of its full-file stream. **The per-class difference between codecs shows where the headroom is**, which
   the per-class absolute cost does not.
4. **Slices.** Our `enwik9_1M/10M/mid10M` slices are in the **original** article order. Mahoney's curve puts the
   census region near the middle of the original file [TD], so **`enwik9_mid10M` may be census-heavy and
   unrepresentative** (UNVERIFIED; check by grepping the slice for census boilerplate). Weight classes by their
   full-file share, not by slice share.
5. **For transformer-based codecs, test on windows of the full preprocessed stream, not on raw slices.**
   - neif's model is trained on its own full-file stream, and its article order, metadata block and 208-symbol
     alphabet are full-file properties: "An input other than enwik9 is not supported" [NEIF] §4.6.
   - neif's own development used "stream prefixes of 20 to 100 MB" and 80 MB windows [NEIF] §3.
   - At about 316 µs per stream byte (ESTIMATE: 175 µs/raw byte × 1e9 / 552.75e6), a 10 MB stream window costs
     about 53 min of one core.

---

## 4. Constraint analysis

### 4.1 Budget per byte (ESTIMATES unless cited)

| Quantity | Value |
|---|---|
| Limit on the AMD reference (T=1310) / Intel (T≈1427) | 53.4 h / 49.1 h [RULES] |
| Project target | ≤45 h → **162 µs per enwik9 byte** (50 h → 180 µs) |
| cmix-neif actual | 175,392 ns/B = 48.72 h on T=1306 [LTCB][NEIF] |
| neif coder stream | 552,750,198 B (after page form, WRT and the metadata transform) [NEIF] |
| At 45 h, per **stream** byte | **293 µs ≈ 1.2M cycles** at ~4 GHz (3700X base 3.6, boost 4.4 GHz; clock assumption **UNVERIFIED** for the test box) |
| Per coded bit | ~37 µs |
| Time split, lexth11c 12-layer model | "transformer 48%, fxcm 29%, cmix mixers 15%, PPMD 2%" [LEXTW] |
| Time split, neif with an earlier 6M model | transformer "about 44% of the run time" [NEIF] §3.1 |
| Implied transformer time per token (neif) | ≈ 0.44 × 175 µs × 1e9/552.75e6 ≈ **140 µs** (upper bound: injected prompt rows also consume tokens) |

### 4.2 What transformer fits?

**Frozen, pre-trained (current practice).**
- At batch size 1, inference is matrix-vector, so every token streams all weights through the core.
- A 7.8M-parameter model held as int8 is about 7.8 MB per token. At about 140 µs per token that is **about 56 GB/s**,
  which is the order of L3 bandwidth for one Zen 2 core. So inference is probably **cache-bandwidth bound, not
  compute bound** (ESTIMATE).
  - The fx2-T writeup says the 3.81-bit weights are decompressed "once at startup" into "a format that takes more
    space during inference" [FX2T]. Whether that format is int8 or int4 is **UNVERIFIED**.
  - Peak int8 throughput (`VPMADDUBSW`, 32 MACs per instruction [FX2T]) is several times higher than the observed
    rate.
- Consequences:
  - Inside about 20–25 h of transformer time, about 8–10M int8 parameters is the ceiling with today's kernels.
  - Keeping weights packed at 4 bits in cache and unpacking in registers could roughly **double** the parameter
    budget per unit of time.
  - So could freeing CM time (lexth11c: pruning 23 → 12 layer-0 mixers costs +0.024% for −10% time [LEXTW]).
- The 3700X has 16 MB of L3 per CCX (vendor spec, not from the prize pages). An int8 model above about 12–14M
  parameters would spill to DRAM and slow down sharply (ESTIMATE).

**Online-trained (nncp-style).**
- Training costs about 6N FLOP per token (forward 2N plus backward 4N), plus attention.
- One Zen 2 core peaks at about 32 fp32 FLOP/cycle (2 FMA × 8 lanes), about 128 GFLOP/s at 4 GHz. Assume about
  50% efficiency for segment-batched training (64 GFLOP/s).
- With 20 h for the network: 4.6e15 FLOP / 553M tokens = 8.3e6 FLOP per token, so **N ≈ 1.4M params, trained for
  one pass** (ESTIMATE). bf16/int8 training tricks might give 3–5M.
- Compare: the frozen model has 7.8M params and 1,000 epochs on the very file being coded. nncp's online model
  needs a GPU (RTX 3090) and still lands at 106.6M [LTCB].
- The FAQ concurs: huge transformers are "too slow for enwik9 to train online", while "pre-trained medium-sized
  Transformers are fast enough by externalizing the high compute demand" [FAQ].

**The double-count tax.**
- Weights appear in both `comp9` and `archive9` [RULES][ZMIX][NEIF].
- neif: 3,716,667 B container / 7.8M params = 0.476 B per param, so **about 0.95 B of S per parameter** (ESTIMATE).
- Average return so far: fx2-cmix → fx2-T cut the payload by about 16.3 MB (ESTIMATE: 110,111,245 − (96,996,198 −
  2,930,652 weights − ~240 KB code)) for about 5.86 MB of weights in S. That is about **2.8 payload bytes per S-byte
  of weights**.
- Marginal return near the frontier:
  - makimono's higher-precision weights returned only about 0.7 payload bytes per S-byte, a loss [MAKI][LTCB].
  - lexth11c's draft judged an 8-layer model (−1.3 MB per copy, +4.1% loss) and shipped 12 layers [LEXTW][LEXT].
  - neif 1.0 grew the container by about 0.87 MB per copy alongside a −2.2 MB archive cut, confounded with other
    changes [LTCB].
- Reading these together: **the parameter-count frontier is around 6–10M at about 3.8 bits/weight** for current
  recipes (ESTIMATE). Plain scaling past it buys little.

**Shipping weights only once?**
- Considered and rejected: the compressor cannot train them. It has the same 50 h single-core budget and must also
  code the file.
- The separate-decompressor relaxation counts `comp9a` + 1×`decomp9` when they are the same binary [RULES]. That is
  still two copies.

### 4.3 How the transformer entries spend the budget

| Entry | Params / shape | Training | Weight storage | Size cost in S | Runtime share |
|---|---|---|---|---|---|
| fx2-cmix-T | 6M; 12 layers, d=192, 9 KDA + 3 SWA (W=1024); byte tokens (205 values); PPMd distribution as input [FX2T][LEXTW] | 320 bf16 + 256 QAT + 16 fp32 epochs on enwik9, 8× RTX 5090, 26 h [FX2T] | int4 in [−7,7] stored at 3.81 bits; LZMA-style range-coded container appended to both binaries [FX2T][NEIF] | 2.9 MB ×2 ≈ 5.9 MB [FX2T] | "about half" of fx2-cmix's time was the LSTM it replaced [FX2T]. 37.2 h total [LTCB] |
| zmix | Same architecture [ZMIX] | Retrained with a weight-size loss term (published recipes) [ZMIX] | Tighter container. Weights carried in `comp9` and `archive9.exe` (same program image) [ZMIX] | comp9 3.22 MB in total [ZMIX] | 36.5 h on T=1724 [ZMIX] |
| lexth11c | 12 layers, MLP 768, 9 KDA + 3 SWA (W=1024) [LEXT] | 1,280 epochs from scratch with QAT, 4× H100 [LEXT][LEXTW] | Lloyd–Max codebook int4 (grid 1/4, \|int\| ≤ 21); 8-bit embedding, prior and unembedding; container 2,978,039 B [LEXT] | ~5.96 MB | Transformer 48% [LEXTW] |
| cmix-neif | 16 layers, d=192, 3 heads × 64, MLP 768; 12 KDA + 4 SWA (W=2048); 208-value vocabulary; inputs: PPMd distribution, two previous tokens, match byte and length class [NEIF] | 1,000 bf16 epochs (warm-started chain) + 32 QAT + 6 fp; about 2,800 GPU-hours, mostly RTX 5090; about €4,000 of compute including CPU testing [NEIF] | Container v4 3,716,667 B: dead neurons pruned, bf16 for unquantized tensors, template-coded names. Appended to both binaries and unpacked at start-up [NEIF] | ~7.43 MB + BITLSTM 7,386 B ×2 | ~44% (earlier model); total 48.7 h [NEIF] |
| makimono | 6M (fx2 architecture) | Distillation from fx2-T's fp model; 5/7-bit QAT; "polish" pass [MAKI] | 3,579,747 B [MAKI] | ~7.2 MB | 63 h (shared VM) [MAKI] |
| rata / fx4 | 6M + 2×200 online LSTM | fx2-T weights | Lossless v5 recompression: 2,930,652 → 2,902,452 B [LTCB] | ~5.8 MB | n/a |
| nncp v3.2 | Online transformer, large (LTCB: 199M for v3) | Online, one pass, on GPU | Nothing shipped | 628,955 B decompressor [LTCB] | GPU required |

---

## 5. Candidate directions to about 95M

**Framing.**
- From the official L (100.42M), "sub-100M" is already achieved by any of the pending entries.
- **About 95M means beating cmix-neif (95.91M) by about 0.9 MB.** That is roughly 1% of its payload.
- The best public neif derivative got 0.14 MB (ex-girlfriend [EXGF]).
- I know of no single published idea that reliably yields 0.9 MB. A credible plan stacks 2–4 of the directions
  below.
- **All gain ranges are SPECULATIVE ESTIMATES** unless a measurement is cited.

### D1. Context injection 2.0: retrieval-augmented conditioning of the transformer (A)

- **Mechanism.** Feed the transformer more *free* context, meaning rows the decoder can derive and that are never
  coded:
  - lower-case and prefix-matching link contexts;
  - kNN-retrieved earlier paragraphs on the same entities, found via hashing of dictionary codes;
  - category and infobox-template exemplars from earlier pages;
  - an article order that puts linking pages before their targets.

  This works against the sliding window and the per-article reset.
- **Evidence.**
  - Dropping neif's rows cost 167 KB at 20% (≈0.8–0.9 MB full), so the model leans heavily on them [NEIF] §4.2.
  - neif §6 lists unexploited sources: 375,169 lower-case links that would add 9.3% more link-context tokens;
    5,025 pages with no link context today; prefix links are 14.6% of the pieces fed today [NEIF].
  - kNN-LM: Wikitext-103 perplexity improved to 15.79, "a 2.9 point improvement with no additional training"
    [KNNLM].
- **Gain.** 0.2–0.8 MB (SPECULATIVE).
- **Cost.**
  - Every injected token costs about one token of inference time; neif skips output heads on unread rows
    [NEIF] §3.4.
  - Needs **retraining for every change in prompt format**: hundreds to thousands of GPU-hours at full scale.
  - RAM: tens of MB for indexes. Decompressor size: a few KB of code.
- **Cheap test.**
  - Proxy: train a 0.5–1M-param byte transformer to *memorize* a 10 MB window of the stream, with and without the
    extra rows. Compare final loss at equal compute. (6N × 10M tokens × 100 epochs ≈ 6e15 FLOP, a few hours on
    12 CPU cores or under 1 GPU-hour; ESTIMATE.)
  - Then code the 10 MB window with a neif build to confirm the effect on the coder.
- **Code.** Builds on neif's prompt machinery (`src/tf1/`, prompt automaton). Retraining uses neif's training tree.

### D2. More compute per weight byte: layer reuse and wider windows, paid for by speedups (A + E)

- **Mechanism.** Under the double-count tax, *compute* is cheaper than *weight bytes*.
  - Loop or share transformer blocks (run blocks twice, or immediate block-wise sharing) to get depth without new
    weights.
  - Widen the SWA window (neif: 2560 beat 2048).
  - Fund it by:
    - int4-in-cache GEMV kernels that unpack in registers, halving bytes streamed per token if inference is
      bandwidth-bound (§4.2);
    - pruning the CM ensemble (half the layer-0 mixers);
    - adaptive depth;
    - output skipping.
- **Evidence.**
  - Window 2560 vs 2048: about −13 KB at 39% of the stream, for +5.5% instructions [NEIF].
  - MobileLLM-LS block-wise weight sharing: "+0.7%/0.8%" accuracy with "no increase in model size" [MOBILELLM]
    (a different domain).
  - Mixer pruning: +0.024% rate for −10% time [LEXTW].
  - Adaptive depth: −5.1% instructions for +18 KB estimated [NEIF].
- **Gain.** 0.2–0.7 MB (SPECULATIVE; looping at this scale is unmeasured).
- **Cost.**
  - Time is the whole point: every 1 h saved is about 2% more transformer compute.
  - RAM negligible; a few KB of code; retraining required.
  - Kernels must stay bit-exact across Intel and AMD (no `rcpps`, fixed accumulation order [NEIF] §1.7).
- **Cheap test.**
  - Kernel speed: microbenchmark on our box on a 10 MB window (no retraining needed for int4-in-cache, which is
    output-identical).
  - Looping: compare proxy-model loss (as in D1) at equal weight bytes.
- **Code.** neif's `src/tf1/` engine and the training tree.

### D3. Better bits per weight: training and quantization frontier (T)

- **Mechanism.**
  - Train longer.
  - QAT with a description-length term (zmix).
  - Non-uniform codebooks (lexth11c).
  - Mixed precision by sensitivity (makimono, but sized to win on **S**, not only on the archive).
  - Structured pruning; entropy-coded containers.
  - Distillation from a larger fp teacher.
- **Evidence.**
  - +8 epochs alone: −57 KB of S [LTCB ft8].
  - zmix: −1.11 MB vs fx2-T. Weights, container and model bank are bundled, but the weight retrain is stated as a
    main change [ZMIX].
  - neif container work: about −137 KB of S [NEIF].
  - makimono shows the trap: archive −302 KB but S +466 KB [MAKI].
  - lexth11c: int3 MLP was rejected, "+3.7% QAT loss vs a 1.9% break-even" [LEXTW].
- **Gain.** 0.1–0.5 MB (SPECULATIVE, but the most predictable item here).
- **Cost.** GPU time (fx2-T used 208 GPU-hours; neif about 2,800 [FX2T][NEIF]). No runtime or RAM cost.
- **Cheap test.** Proxy models on a window, comparing loss × weight bytes. The full-scale result needs a full
  retrain plus one full encode (about 49 h).
- **Code.** neif training tree (Ivanov's trainer + neif overlay). zmix's objective is described but its "training
  scripts are not part of this package" [ZMIX].

### D4. Online adaptation of the frozen transformer (A)

- **Mechanism.** Dynamic evaluation: update a small adapter while coding. Candidates:
  - fast weights on the last-layer features (ex-girlfriend);
  - LoRA on the last 1–2 blocks;
  - an online-updated unembedding bias.

  Both sides learn only from already-coded symbols.
- **Evidence.**
  - ex-girlfriend: the fast-weight head alone gave −46 KB on a full encode, at +12.9% time on a 160 MB screen; its
    whole bundle gave −135 KB [EXGF].
  - Literature: Transformer-XL on enwik8 went from 0.99 to 0.94 bits/char with dynamic evaluation [DYNEVAL]. But
    that model was evaluated on *held-out* text. **Our model is trained on the test file itself**, so most of that 5%
    should already be absorbed. Expect far less.
- **Gain.** 0.05–0.4 MB (SPECULATIVE; long shot at the high end).
- **Cost.** +5–15% time per adapted block (backward through the adapter). RAM: a few MB. No stored weights.
- **Cheap test.** On a 10 MB window with neif's frozen model, compare coded bytes with and without the adapter.
  CPU only, about 1 h per run.
- **Code.** ex-girlfriend source (`PORT_FW` in `src/tf1/opt/model_opt.cpp` [EXGF]) on top of neif.

### D5. Specialists for structured and numeric regions (P + M)

- **Mechanism.** Use exact or derivable structure that neither the CM nor the transformer can learn:
  - forecasts for metadata (revision, page id, timestamps, contributors);
  - census and calendar generators;
  - table and infobox field models;
  - date and number arithmetic (ISBN-style check digits, sums, ages from birth/death years);
  - `[[link]]` target prediction from the title set.
- **Evidence.**
  - Metadata block: 1.35 MB at 2.17 bits/byte.
  - Census pages: 3.58 MB.
  - Tail classes: 2.82 MB [NEIFRATE].
  - ex-girlfriend E1+E3b: −22.8 KB on 12 MB [EXGF].
  - neif census transform: about −93 KB in total; ISBN check digit: −10 to −13 KB; number/date contexts: about
    −60 KB (estimate) [NEIF].
- **Gain.** 0.1–0.4 MB (SPECULATIVE; bounded because these regions are under 10% of the payload).
- **Cost.**
  - CPU-only and testable on slices.
  - Tiny time and RAM.
  - Each new stream symbol "costs several KB up to a few tens of KB" [NEIF] §4.6.
  - Changes to stream form that the transformer sees require retraining.
- **Cheap test.** Per-class cost logging (§3.2) on 10 MB windows. Most items run in minutes.
- **Code.** neif (metadata block, census transform), plus fresh code.

### D6. Post-mixer correction and learned mixing (M)

- **Mechanism.**
  - Larger or deeper offline-trained correction nets (BITLSTM successors).
  - Learned SSE.
  - An offline-trained final mixer whose weights ship (small).
  - Retune mixer learning rates and schedules against the transformer-dominated inputs.
- **Evidence.**
  - BITLSTM: about −0.3 MB vs none (estimate) and −32 KB of S vs an untrained online version [NEIF].
  - obias head: 23 KB of weights [OBIAS].
  - Mixer learning-rate retunes: −33 KB and −46 KB [NEIF].
  - RATA's SPECIALIST corrector [RATA].
- **Gain.** 0.05–0.3 MB (SPECULATIVE).
- **Cost.** +3–4% time per BITLSTM-scale net [NEIF]; KB-scale weights ×2.
- **Cheap test.** Traces from a 10–20% prefix (neif's recipe [NEIF] §5); replay training on CPU.
- **Code.** neif `training/bitl/`.

### D7 (long shot). Token-level transformer over dictionary codewords (A)

- **Mechanism.** Predict whole dictionary codewords (word tokens) instead of bytes. That gives fewer transformer
  steps per byte, so more parameters per step within the time budget, and a longer effective context per window.
  Feed bit-level probabilities to the CM through a hierarchical softmax over the codeword tree. zmix's word-label
  model already does this, with a log-bilinear model [NEIF] §3.3.
- **Evidence.**
  - nncp codes enwik9 with a 16,384-symbol vocabulary from its own preprocessor [LTCB #1072].
  - The word-label model gives −4.3 KB on 80 MB, showing the bit/word bridge works [NEIF].
  - No CPU-budget entry has tried this.
- **Gain.** Unknown; could be negative (SPECULATIVE).
- **Cost.** Large: new training pipeline, a new inference engine for the vocabulary-softmax path, and handling of
  non-dictionary bytes. Fresh code. Months.
- **Cheap test.** Proxy memorization experiment on a 10 MB window (token vs. byte model at equal FLOPs and equal
  weight bytes), measuring bits per stream byte.

### D8 (not recommended). Replace the pre-trained model with a CPU online-trained transformer (A)

- §4.2 gives about 1.4–5M params for a single pass, against 7.8M params trained for 1,000 epochs on the same file.
- nncp needs a GPU and reaches 106.6M [LTCB].
- I would only revisit this as a *small add-on* beside the frozen model. Even there, lexth11c measured "an online
  LSTM beside the transformer" as rejected [LEXTW], while neif kept an 80-cell one for about 12 KB [NEIF]. Expect
  little.

**Ranking (my judgment).**
1. **D1** context injection 2.0: largest evidenced lever, but needs retraining.
2. **D2** compute-per-byte: attacks the actual binding constraint (time).
3. **D3** better bits per weight: most predictable.
4. D5 and D6: cheap, CPU-only, steady 50–300 KB items to stack.
5. D4: moderate.
6. D7 and D8: long shots.

---

## 6. Recommended base codebase(s)

**Primary: cmix-neif-1.0.** Reasons:
- It is the best public result (95.91M, pending).
- Its README is the most thoroughly documented in the lineage: per-item measurements, determinism rules, training
  pipeline, memory and paging design [NEIF].
- It already integrates the useful ideas from fx2-T, obias, zmix and fx-deepmix.
- It builds for `-march=x86-64-v3` only, with no CPUID dispatch. That matches our AVX2 rule [NEIF] §1.7.

Caveats:
- GPL-3.0-**only**, so our derivative must be GPL-3. That is OSI-approved and acceptable [RULES].
- Only about 4.9 h of headroom on T=1306 hardware, so any added model must be paid for in speed first.
- Mahoney measured 82.87 h decode on the Intel WSL2 box [LTCB]. Which machine's timing binds is an open question.
- Retraining needs neif's training tree. The 228 GB prior dumps and 40 GB BITLSTM traces are **not shipped**; they
  are regenerated by build variants of the compressor [NEIF] §5.

**Secondary: fx2-cmix-transformer-v1** (the official record):
- the cleanest place to do controlled ablations of transformer changes (the only change from fx2-cmix is the
  transformer);
- the original training recipes (`training_recipes.train_6m`, `quantize`, `cooldown`) [FX2T].

**Reference only (read, borrow mechanisms):**
- zmix 1.0 (Zig; compact decompressor; weight-size-aware objective) [ZMIX];
- ex-girlfriend (fast-weight adaptation code) [EXGF];
- lexth11c (PPMd pruning fix, codebook QAT, mixer pruning study) [LEXT];
- cmix-obias (BITLSTM origin) [OBIAS];
- fx2-cmix (article-order embedding pipeline) [FX2].

**What to fetch once cloning is approved** (SHAs from the GitHub API on 2026-11-04 [GHAPI]):

| What | Where | Pin |
|---|---|---|
| cmix-neif source + training tree + weight container + PGO profile | `https://github.com/marwoelk/cmix-neif-1.0` (also `https://marwoelk.de/cmix-neif-1.0/cmix-neif-1.0.tar.gz`) | commit `1c9ea5ed57ec9718bcb681d20b79e81a60b7a728` (5 Oct 2026; ex-girlfriend pins the same commit [EXGF]). Release `comp9` sha256 `8b07d85f…d7` and `archive9` sha256 `30eaf239…4f18` [NEIF] |
| Toolchain for byte-identical neif `comp9` | Ubuntu 24.04, clang/lld/llvm 18.1.3 via neif `install.sh` [NEIF] | Our g++ 15 build will produce a *different* (still self-consistent) binary and archive [NEIF] §1.7 |
| fx2-cmix-transformer-v1 | `https://github.com/astOwOlfo/fx2-cmix-transformer-v1` | commit `83f2603f3f7751da5f429fd32669807eb8510494` (2 Sep 2026) |
| ex-girlfriend | `https://github.com/rlanday/ex-girlfriend` | commit `e923faec085a93d24bf519986ee2881497e97934` (14 Oct 2026) |
| cmix-lex-transformer lexth11c | `https://github.com/Neel49/cmix-lex-transformer` | commit `653f64e5083e430503fbe3d885def946891e7ed0` (27 Sep 2026) |
| zmix 1.0 | `https://zmix.frostbyrne.io/v1.0/zmix-src.tar.gz` (Zig 0.15.1, UPX 5.2.0) [ZMIX] | tarball (no git SHA found; **UNVERIFIED** where the repo lives) |
| cmix-obias | `https://huggingface.co/dfreelan/cmix-obias` | source commit `60c499e` [OBIAS] |
| fx2-cmix (article-order notebooks) | `https://github.com/kaitz/fx2-cmix` | commit `04c5806f99b9b0fa8572be8c8063b4324ec405de` |

**First milestones.**
1. Build neif with `-march=x86-64-v3`.
2. Round-trip a 10–20 MB stream window, then measure rate per region against the published TSV.
3. Do one full enwik9 encode and decode to reproduce about 95.9M on our box (about 49 h each way) before changing
   anything.

That reproduction alone puts us under 100M. It is not our contribution, though, and it gives no prize standing.

---

## 7. Biggest open questions

1. **Pending verification.** Which of zmix, lexth11c and cmix-neif get accepted, and in what order? If neif is
   accepted, the prize bar is about 94.95M and our ~95M target is no longer prize-relevant [RULES][LTCB].
2. **GPU budget.** Every transformer direction (D1–D3, D7) needs retraining: fx2-T used 208 GPU-hours, neif about
   2,800. AGENTS.md lists no GPU on the dev box. We need a decision on rented GPUs.
3. **Binding timing.** The AMD runs give 48.7 h (neif) and 37.2 h (fx2-T). The Intel WSL2 runs give 82.87 h (neif)
   and 8 days (fx2-T). fx2-T was awarded anyway [LTCB]. How much headroom we really have under 53.4 h is the key
   design input for D2.
4. **Is the inference engine bandwidth-bound or compute-bound?** My 56 GB/s inference is an ESTIMATE. It decides
   whether int4-in-cache kernels buy roughly 2× parameters or nothing. Measure on our box first.
5. **Marginal return of parameters.** No published controlled ablation of transformer size on the full file at
   equal training. makimono's +466 KB is the only clean S-level data point.
6. **Per-class headroom.** Unpublished. §3.2 is the first measurement to run.
7. **Discrepancies in official totals.** phda9 (+85,888 B between [HP] and [LTCB]), fx-cmix (README vs [HP]),
   cmix-obias (+29,045 B between its submission doc and [HP]), and the makimono LTCB total typo. They are not
   load-bearing for us but are UNVERIFIED.

---

## Sources

All retrieved 2026-11-04 unless noted.

- **[LTCB]** Matt Mahoney, Large Text Compression Benchmark, https://mattmahoney.net/dc/text.html ("Last update: Oct.
  14, 2026"). Entry anchors cited as #NNNN (e.g. #0919 cmix-neif, #0958 lexth11c, #0960 zmix, #0969
  fx2-cmix-transformer and ft8, #0968 rata-cmix, #1072 nncp, #1080 cmix-obias, #1091 cmix-lex / fx-cmix / fx2-cmix
  / fx3-cmix, #1137 cmix-hp / fast-cmix, #1149 starlit, #1165 phda9).
- **[TD]** Mahoney, About the Test Data, https://mattmahoney.net/dc/textdata.html
- **[HP]** Hutter Prize main page, http://prize.hutter1.net/ (fetched over HTTP; record table and source links).
- **[FAQ]** Hutter Prize FAQ, http://prize.hutter1.net/hfaq.htm (#largenn, #getsource).
- **[RULES]** This repo's `docs/RULES.md` (itself sourced from [HP], [LTCB] and the rules page).
- **[STARLIT]** https://github.com/amargaritov/starlit (README).
- **[FASTCMIX]** https://github.com/saurabhk/fast-cmix (README, including the cmix-hp section).
- **[FX]** https://github.com/kaitz/fx-cmix/blob/main/README.md
- **[FX2]** https://github.com/kaitz/fx2-cmix/blob/main/README.md
- **[FX3]** https://github.com/kaitz/fx3-cmix/blob/main/README.md
- **[LEX]** https://github.com/blahem/cmix-lex/blob/main/README.md
- **[LEXCH]** https://github.com/blahem/cmix-lex/blob/main/changes.md
- **[OBIAS]** https://huggingface.co/dfreelan/cmix-obias (README.md, SUBMISSION.md)
- **[FX2T]** https://github.com/astOwOlfo/fx2-cmix-transformer-v1/blob/main/writeup.md and README.md
- **[ZMIX]** https://zmix.frostbyrne.io/v1.0/writeup.md and https://zmix.frostbyrne.io/v1.0/README.md
- **[LEXT]** https://github.com/Neel49/cmix-lex-transformer/blob/main/README.md
- **[LEXTW]** https://github.com/Neel49/cmix-lex-transformer/blob/main/docs/HUTTER_WRITEUP.md (draft dated
  2026-09-11; where it conflicts with [LEXT] on the shipped model, [LEXT] wins)
- **[NEIF]** https://github.com/marwoelk/cmix-neif-1.0/blob/main/README.md and https://marwoelk.de/cmix-neif-1.0/
- **[NEIFRATE]** https://github.com/marwoelk/cmix-neif-1.0/blob/main/doc/stream_rate.tsv (region aggregation is
  mine)
- **[EXGF]** https://github.com/rlanday/ex-girlfriend/blob/main/README.md
- **[MAKI]** https://github.com/krapuchino83/makimono/blob/main/README.md
- **[RATA]** https://github.com/Dharmesh2015/RATA-CMIX (README)
- **[NNCP]** https://bellard.org/nncp/
- **[TSZIP]** https://bellard.org/ts_zip/
- **[DYNEVAL]** Krause et al., Dynamic Evaluation of Transformer Language Models, https://arxiv.org/abs/1904.08378
- **[KNNLM]** Khandelwal et al., Generalization through Memorization: Nearest Neighbor Language Models,
  https://arxiv.org/abs/1911.00172
- **[MOBILELLM]** Liu et al., MobileLLM, https://arxiv.org/abs/2402.14905
- **[GHAPI]** GitHub REST API repository and commit metadata for the repos above (license `spdx_id`, latest commit
  SHA), https://api.github.com/repos/...
- **[SEARCH]** Web search 2026-11-04, "enwik9 compression breakdown by content type markup article text bits per
  byte analysis". No per-content-type breakdown found.
