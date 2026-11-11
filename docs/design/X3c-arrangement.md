# X3c: the sciences of arrangement (what sits next to what, and in what order)

Date: 2026-11-11. Classification: **preprocessing** (article order), offline pricing + real-stack samples. One of three
parallel cross-domain darts (X3a, X3b, X3c). Lens: seriation and stratigraphy, genome assembly, phylogenetic leaf
ordering, TSP, library classification, columnar databases, curriculum design, caching theory, bandwidth-minimizing
matrix orderings.

**Verdict: NO-GO.** Eighteen arrangement candidates were counted; none reaches the 40 KB bar with support, none
approaches 100 KB. The record's article order turns out to be right for this stack at every scale above about a
thousand articles, and the one lead is the scale below that.

- **What the reorder costs.** The permutation is paid twice: 181,132 B as the order stream in comp9 and about 216 KB
  again in the archive, because the metadata block's page-id deltas are how the decoder learns the order (10.38 bits per
  article instead of about 1). About 397 KB of S before it earns anything (§1).
- **The order-sensitivity bound is 3,079,208 B (24.6 Mbit) and it is not a bound.** That is transformer-alone minus
  final over the C3 full run. But the transformer takes PPMD's distribution as an input: the same 1,324 articles cost
  the transformer alone 12.4% less in the warm full run than in a cold slice, and per article the stack's cost follows
  PPMD-alone's at 0.34–0.59 B per byte (r 0.87–0.99, five pairs). The transformer **is** order-sensitive, through its
  prior (§2).
- **Proxy:** the stack's own PPMD component alone on 100 MB windows (5 minutes each), times that slope (§4).
- **Identity order: NO.** It would save the 397 KB and lose an estimated 540–930 KB of text (§5.2).
- **Seriation by shared text (MinHash overlap chain): NO**, 0.03–0.48% worse than the record's order for PPMD (§5.3).
- **Windowed id-sort (K2): a lead, about 45 KB [ESTIMATE], range +25 worse to 85 better.** Keep the record's coarse
  order, sort by page id inside windows of 256–1024 articles; no code, RAM or time. The order stream shrinks by 37–71 KB
  (measured with cmix), the metadata block by 23–62 KB (real stack, cold proxy), the text grows by 14–83 KB plus an
  unresolved 0–60 KB. Not a GO: it straddles the bar and its larger half vanishes if X3a's rule replaces the shipped
  order (§5.1).
- **Side finding that matters for the next full run:** X2 drops the page-id delta of every page with a `<restrictions>`
  element (1,013 in the full run), so its redirect law runs on a base that is off by millions, and `try_rpid()` reads a
  bitmap at a negative index when the drift goes below zero. In C3's run the tracked id never went negative; a record-order sample
  of the first pages segfaults twice at the same address. **Any change of article order can crash the compressor at
  about 95% of a full run** until that is fixed (§7.1).

Labels: **[M]** measured here, **[ESTIMATE]** derived from measurements by the argument next to it.

## 0. Method and reproduction

| Step | Script |
|---|---|
| Per-page and per-MiB sums of the C3 full-run cost dump (streamed) | `scripts/analysis/x3c_pages.py` |
| Region split (text / metadata block per field / interlanguage block) | `scripts/analysis/x3c_tail.py` |
| Stakes: titles, redirects, position deciles (curriculum) | `scripts/analysis/x3c_fields.py` |
| Is the transformer order-sensitive? Same articles, two histories | `scripts/analysis/x3c_tfsens.py` |
| Samples of whole pages in any order (also metadata proxies, windowed sorts, full-file reorders) | `scripts/analysis/x3c_mksample.py` |
| Real stack on a sample (C4 costlog twin, slot + bench.sh, round trip, ledger row, cost log kept) | `scripts/analysis/x3c_bench.sh` |
| Two real-stack cost logs of the same page set in two orders: regions, per-article deltas, beta | `scripts/analysis/x3c_pair.py` |
| The real PPMD component alone over a stream (`cmix --ppmd-only`) | `scripts/analysis/x3c_ppmdonly.sh`, driver `x3c_ppmd_windows.sh` |
| Region costs of PPMD-only runs | `scripts/analysis/x3c_ppmdcmp.py` |
| The windowed id-sort family: order files, rank-delta streams, page-id delta streams | `scripts/analysis/x3c_orders.py` |
| Real coded size of an order stream (cmix, as the build codes comp_order) | `scripts/analysis/x3c_ordersize.sh` |
| Seriation by shared text (MinHash of word 4-grams, greedy chain) | `scripts/analysis/x3c_seriate.py` |
| Page id from alphabetical-neighbour titles / the last K records | `scripts/analysis/x3c_pidlaw.py` |
| Replay of X2's page-id tracking over a stream (the drift and crash of §7.1) | `scripts/analysis/x3c_x2drift.py` |
| A small lpaq-like CM (first proxy attempt, not used for any number here; see §4.3) | `scripts/analysis/x3c_proxy.cpp` |

```sh
python3 -P -E scripts/analysis/x3c_pages.py --out build/exp/X3c/pages_full.npz      # ~10 min, streams 23 GB
python3 -P -E scripts/analysis/x3c_tail.py   > build/exp/X3c/tail.txt
python3 -P -E scripts/analysis/x3c_fields.py > build/exp/X3c/fields.txt
python3 -P -E scripts/analysis/x3c_tfsens.py full build/exp/A1/runs/cost_10M.bin id
S=build/exp/X3c/samples; M=scripts/analysis/x3c_mksample.py
python3 -P -E $M --set bytes:rank:60000:10000000 --order rec --out $S/B10_rec.xml   # 1,518 pages from record rank 60000
python3 -P -E $M --set bytes:rank:60000:10000000 --order id  --out $S/B10_id.xml    # same pages sorted by page id
scripts/analysis/x3c_bench.sh $S/B10_rec.xml 'notes'; scripts/analysis/x3c_bench.sh $S/B10_id.xml 'notes'
python3 -P -E scripts/analysis/x3c_pair.py B10_rec B10_id
scripts/analysis/x3c_ppmd_windows.sh                                                # PPMD-only windows (model file on /dev/shm)
python3 -P -E scripts/analysis/x3c_orders.py 1 4 16 64 256 1024 4096 16384 200000
scripts/analysis/x3c_ordersize.sh 1 256 1024 4096 16384
python3 -P -E scripts/analysis/x3c_pidlaw.py
python3 -P -E scripts/analysis/x3c_x2drift.py build/exp/E12/full/.ready4cmix          # 1,013 dropped deltas, drift +2,740,978
```

Scratch is in `build/exp/X3c/` (gitignored). Inputs reused: `build/exp/FULL/cost_full.bin` (C3 full run),
`build/exp/E12/full/` (the -e intermediates: `.main_reordered`, `.ready4cmix`, `e9.stream`), `build/exp/A1/full/full.pre`
(identity-order stream), `build/exp/X2/meta_pages.tsv` (the full run's page order).

## 1. How the arrangement works today [M]

- **Text order.** `comp9 -e` reorders the 243,425 pages of `.main` before phda9 (`article_reorder.h`): the 172,277
  non-redirect articles in the order of `new_article_order` (kaitz/fx2-cmix: voyage-large-2 embeddings, t-SNE to 1-D,
  k-means), then the 71,148 redirects in id order. The order was derived for fx2-cmix, a pure CM; nobody re-derived it
  for a stack where a memorized transformer does most of the work.
- **Where the order lives.** Only in comp9 (`comp_order`, CM-coded; P1's rank-delta recoding: **181,132 B**, counted once
  in S). archive9 carries no order: the decoder restores the original order by sorting pages by page id (`sort()`).
- **The order is paid for twice.** The decoder can only sort because the page ids reach it, and they do so as the
  metadata block's page-id delta lines, which in record order are the permutation again: **251,063 B** in the C3 full
  run (articles 223,606 B = 10.38 bits each; redirects 27,458 B = 3.09 bits). In id order they cost about 1 bit each
  (X2's id-order control: 1.14 bits, about 35 KB). So the reorder costs about **181 + 216 = 397 KB of S** before it
  earns anything: the same permutation is shipped once in comp9 and once in the archive.
- **Blocks.** phda9 moves each page's revision header to a metadata block after the last page (15.7 MB coded, 1,411,027 B)
  and trailing interlanguage links to a block after that (15.9 MB coded, 1,365,138 B). Both inherit the text order.

Region split of the C3 full run (`x3c_tail.py`):

| region | coded B | final B | transformer-alone B | G = tf − final | G % | PPMD-alone B |
|---|---:|---:|---:|---:|---:|---:|
| article text | 554,867,444 | 90,535,425 | 93,401,079 | 2,865,654 | 3.17 | 151,808,720 |
| metadata block | 15,717,620 | 1,411,027 | 1,549,811 | 138,784 | 9.84 | 2,803,616 |
| interlanguage block | 15,867,681 | 1,365,138 | 1,439,875 | 74,737 | 5.47 | 3,347,243 |
| intro + coda | 6,275 | 1,444 | 1,477 | 33 | | 2,480 |
| **all** | 586,459,020 | 93,313,033 | 96,392,241 | **3,079,208** | 3.30 | 157,962,059 |

Metadata block per field (243,425 records): revision id 15.57 bits (473,695 B), timestamp 9.18 (279,444 B), page-id
delta 8.25 (251,063 B), user name 9.39 (241,122 B), ip 24.09 (117,551 B), user id 1.86 (47,734 B), tags 0.00.
Page-id delta by tenth of the block: 11.66 12.32 11.88 11.67 8.84 5.72 10.55 3.61 3.28 2.97 bits.

## 2. The order-sensitivity bound, and why it is not a bound [M]

**The brief's number.** The gap between the transformer-alone cost and the final cost is **G = 3,079,208 B = 24.6 Mbit**
(3.30% of the payload; text 2.87 MB, metadata 139 KB, interlanguage 75 KB). That is everything the online side (CM,
match models, PPMD as a mixer input, E2, BITLSTM, E12, X2) adds over the transformer. G is flat along the file:
2.4–3.0% in the first seven tenths of the text, no warm-up deficit in the first MiBs (3.75% / 2.80% / 5.44% / 3.19% in
MiB 0–1 / 1–2 / 2–4 / 4–8), 6–7% in the bot-generated last two tenths.

**But the transformer is not order-invariant.** `docs/refs/fx2-cmix-transformer.md` says the reorder "doesn't matter to
the transformer (each article is a fresh context)". The context is fresh, but its *input* is not: every step takes PPMD's
205-way distribution as a prior (`TransformerByteUpdate`), and PPMD is an online model of the whole history. Measured
(`x3c_tfsens.py`): the same 1,324 articles (5,350,863 coded B, the 10M slice's pages) cost

| | cold, id order (10M slice, record build) | warm, record order (C3 full run) | change |
|---|---:|---:|---:|
| PPMD alone | 2,064,854 | 1,691,215 | −18.1% |
| **transformer alone** | 1,234,963 | 1,081,269 | **−12.4%** |
| final | 1,225,357 | 1,049,461 | −14.4% |

Per article, the final-cost change is **0.46 × the PPMD-alone change (r = 0.984)** and the transformer-alone change is
0.40 × (r = 0.986), steady across the slice (0.45 / 0.46 / 0.49 / 0.50 by quarter). So the transformer leans on its PPMD
prior for whatever it has not memorized, and anything that changes what the history offers PPMD moves the whole stack at
about 0.46 B per PPMD byte. **The order-sensitive mass is therefore not bounded by G**; it has to be measured. It also gives
the proxy of §4: the real PPMD component alone is cheap to run (about 6 µs per coded byte) and the stack follows it.

(The two runs differ in build, record vs C3, by about 0.3%; in warmth; and in order. The comparison shows the size of the
dependence on history, not the order effect alone. §4 measures the order effect with the build held fixed.)

## 3. Survey: 18 arrangement candidates

"Inverted by" = how the decoder undoes it. Every reorder of whole pages is inverted for free by the existing sort by
page id, but pays in the page-id channel (§1); that conservation law prices most of the table.

| # | Field: principle | Arrangement | Inverted by | Counted? Number, verdict |
|---|---|---|---|---|
| K1 | Stratigraphy: leave the layers in deposition order | **Identity order**: drop the reorder, the order stream and its reader | already sorted | Saves 397 KB (181 order + 216 page ids). Costs: PPMD text +0.50% for id vs record order on the first 100 MB of pages (a tenth of the neighbours; 260–450 KB at β 0.34–0.59), and +0.54% more when only a 14 K-article window is id-sorted at full density (279–484 KB); the two scales add. **NO** (§5.2) |
| K2 | Seriation at two scales; columnar sort-key prefix | **Windowed id-sort**: coarse order from the record, pages sorted by id inside windows of W | sort by id | W 256–1024: order stream −37 to −71 KB (cmix), metadata block −23 to −62 KB (real stack, cold proxy), text +14 to +83 KB (PPMD-only × β), plus an unresolved 0–60 KB. **About +45 KB, range −25 to +85 [ESTIMATE] → a lead at the bar, not a GO** (§5.1) |
| K3 | Stratigraphy / record linkage: order = f(metadata), block coded first (revision id = last-edit time; last editor) | decoder recomputes it; no order stream, sorted ids | f is recomputed | Dominated by K1: id-adjacent pairs are within 10 record ranks 15.0% of the time (within 1,000: 30.4%), revision-id-adjacent pairs 7.3% (18.1%), random 0.0% (1.2%). Top-20 editors hold 18% of articles. **NO** |
| K4 | Library classification: a rule generates the order inside the compressor | none needed (compressor only) | sort by id | Stake 181 KB minus code ×2. **X3a built this** (`patches/exp/X3a-order-rule`: the record's order as rule + residual, 181,132 → 76,846 B per its commit 55e6315); not duplicated here. Interaction: it removes most of K2's larger half, and K2's windowed order would need its own rule |
| K5 | Columnar: block-specific order (metadata sorted by id, text in record order) | permutation shipped in archive9 | shipped | X2's page-id law re-priced with P1's stream: 165.4 KB saved, 181.1 KB shipped = **−16 KB. NO** (X2's pricing stands) |
| K6 | Genome assembly (overlap–layout–consensus): seriate by shared text, not by embedding | greedy nearest-neighbour chain on MinHash Jaccard of word 4-grams | sort by id | 100 MB window: PPMD text **+0.48%** (worse), metadata +7.5%; restricted to local re-chaining (next 64 pages): +0.03%, still worse. **NO** (§5.3) |
| K7 | Curriculum design: easy first / hard last for the online learners | sort by difficulty | sort by id | No learning curve to exploit: G is 2.4–3.0% from the first MiB on (§2). The cold→warm change is history content, not learner maturity, and any macro reorder pays K1's loss. **NO, 0 KB lever** |
| K8 | Logistics / join order: redirects next to (or sorted by) their targets | redirects sorted by target rank | sort by id | Redirect pages cost 374,982 B (42.2 bits: title 22.7, target ≈ 19.5). Monotone targets could save ≤ 17 bits × 71 K ≈ 150 KB with a new specialist, but the redirects' ids stop being sorted: 3.09 → ≈ 16 bits, ≈ +115 KB. **≤ 35 KB before code. NO** |
| K9 | Library catalogue: a sorted title column first, order = f(titles) | decoder sorts titles | recomputed | Titles cost 563,866 B (articles 16.8 bits, redirects 22.7). The transformer predicts a title in place; a moved column is new content for frozen weights. **Not testable under the constraints; stake noted** |
| K10 | Columnar database: interlanguage block transposed by language | per-page language mask | recomputed | Block 1,365,138 B, T1 already copies titles. Changes the layout the transformer memorized. **Not tested (frozen weights)** |
| K11 | Columnar: metadata block split into field columns | fixed | fixed | Same constraint (neif: −62 KB with a retrained model). **Not tested** |
| K12 | Record linkage: page id from the alphabetically nearest decoded title (batches were created alphabetically) | none (a specialist) | — | Mean log2 distance 13.55 bits vs 7.69 for the previous record; best of two 7.40. **≤ 0.3 bit, NO** (`x3c_pidlaw.py`) |
| K13 | Caching / working sets: page id from the last K records | none | — | K = 2…64: 7.67…7.14 bits before paying log2 K. **NO** |
| K14 | Optimal paragraphing (Knuth–Plass): choose the 128 K-token piece cuts | none | — | 15 articles exceed 2^17 tokens (22 cuts). **0** |
| K15 | TSP with a coding-cost term: optimize text cost + order cost + id cost together | new order file | sort by id | K2 is its crudest form; K1/K2/K6 bracket it: the fine order is worth less than its bits, the coarse order more. **Lead** (§7) |
| K16 | Phylogenetics: optimal leaf ordering / flipping of the k-means clusters | new order file | sort by id | Macro grouping matters (K1: 0.50% of PPMD text at 1/10 density), so cluster adjacency may too; needs cluster labels and full-scale PPMD runs. **Not tested** |
| K17 | Numerical linear algebra: Cuthill–McKee / spectral order of the link graph (neif: linking pages before targets) | new order file | sort by id | Same test as K6/K16; neif used it with prompt injection, which we do not have. **Not tested** |
| K18 | Encoder-side order search with the real stack | — | — | One evaluation is hours. **Infeasible** |

## 4. The proxy and its validation

### 4.1 Proxy: the real PPMD component alone, times a measured transfer coefficient β

No full run was allowed, and a 10 MB real-stack run took 1.5–3 h per direction on the shared box. The proxy is
`cmix --ppmd-only` (the stack's own PPMD, order 25, the exact code and parameters; about 6 µs per coded byte, a 100 MB
sample in 5 minutes) on a stream in the candidate order, and then

  Δ(final cost) ≈ β × Δ(PPMD-alone cost).

Harness check: `--ppmd-only` on the 10M slice gives 2,159,204 B; the record's cost log sums PPMD to 2,158,032 B (+0.05%).

### 4.2 Validation of β [M]

| perturbation | articles | Δ PPMD alone | Δ final | **β** (per-article regression through 0) | r |
|---|---:|---:|---:|---:|---:|
| history: cold id-order 10M slice (record build) → warm record-order full run (C3) | 1,324 | −373,639 B | −175,896 B | **0.458** (ratio of sums 0.471) | **0.984** |
| history: cold 10 MB record-order window `B10_rec` (C4) → the same articles in the full run (C3) | 1,517 | −191,729 B | −106,552 B | **0.527** (0.556) | **0.923** |
| order, real stack: metadata proxy `M100k` (2,800 pages of ranks 100,000+, text stubbed), record order → W = 1024 | 2,800 | +114 B (Σ\|d\| 3,546) | +899 B (Σ\|d\| 2,321) | **0.593** | **0.877** |
| order, real stack: `M100k`, record order → fully id-sorted | 2,800 | +236 B (Σ\|d\| 3,835) | +1,224 B (Σ\|d\| 2,568) | **0.581** | **0.865** |
| order, real stack, **real text**: `R60000_1` (119 pages from record rank 60,000, 1 MB), record order → id-sorted | 119 | +124 B (Σ\|d\| 16,313) | +121 B (Σ\|d\| 5,611) | **0.336** | **0.988** |
| order, real stack, real text, 10 MB: `B10_rec` → `B10_id` (1,518 pages) | | | | not finished: `B10_rec` 1,283,771 B (round trip OK); `B10_id` waited 80 min for a slot and was cancelled (see below) | |

So the per-article slope is β = 0.34–0.59 across two kinds of history change and three order changes, with correlations
0.87–0.99: **the proxy tracks the real stack article by article.** What it does not carry is a second, smaller term.
Fitting Δfinal = β·ΔPPMD + c per article:

| pair | slope β | intercept c, B per article (s.e.) |
|---|---:|---:|
| `M100k` record → W = 1024 (stub pages) | 0.590 | +0.30 (0.01) |
| `M100k` record → id-sorted (stub pages) | 0.575 | +0.39 (0.01) |
| `R60000_1` record → id-sorted (real text) | 0.336 | +0.67 (1.84) |

c is order loss that does not pass through PPMD (CM recency, a title that no longer follows a similar title). On stub
pages, where titles are adjacent, it is 0.3–0.4 B per article, which would be 50–70 KB over 172 K articles; on real text
the 119-article pair cannot tell 0 from that (the net sums there, +121 B final for +124 B PPMD, are within noise of
either). A 1,518-article pair would have s.e. ≈ 0.5 B per article, i.e. ±85 KB at full scale, so `B10_id` could not have
settled it either; that is why it was cancelled rather than waited for. **Status: slope validated (r 0.87–0.99);
intercept unresolved, 0 to about 60 KB at full scale.** Estimates below use β = 0.34–0.59 and state the intercept
separately.

### 4.3 What did not work as a proxy

- **A small lpaq-like CM** (`x3c_proxy.cpp`, 9 hashed orders + 2 match models; 10M slice 2,210,370 B): at full scale it
  crawled (64 MB in 9 minutes next to three real runs), had no validation, and models the CM side, which turned out to
  be the smaller channel (the transformer follows PPMD). Dropped; no number here comes from it.
- **Full-scale PPMD-only runs** (identity order, record order, W = 4096 over all of enwik9): three runs with the 14 GB
  model file on btrfs produced about 1 GB/s of writeback, stalled the box (0.2% per minute) and two real runs were
  OOM-killed around them. Stopped at 3.5%. The 100 MB windows below (model file on `/dev/shm`, 1.2 GB each) replace
  them. **The identity-order cost at full scale was therefore not measured**; K1 is priced from the windows.

## 5. Experiments

### 5.1 K2, the windowed id-sort

Order family: take the record's order and sort the article numbers inside consecutive windows of W ranks
(`x3c_orders.py`; W = 1 is the record's order, W = 172,277 the identity order). No code changes: only the order file.

**Savings, channel 1: the order stream** (rank-delta text of P1, counted once). Quick sizes with xz -9e / 7z PPMd o6, and
the real size with cmix as the build codes it (`x3c_ordersize.sh`):

| W | 1 | 4 | 16 | 64 | 256 | 1024 | 4096 | 16384 | ∞ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| order stream, 7z PPMd | 202,028 | 240,572 | 218,375 | 199,749 | 160,105 | 122,291 | 87,722 | 54,843 | 330 |
| order stream, **cmix (real)** | **181,132** | | | | **144,186** | **110,527** | **79,703** | **50,469** | 0 |
| page-id delta lines, 7z PPMd | 277,700 | 323,554 | 295,753 | 275,423 | 234,573 | 195,675 | 160,418 | 127,043 | 65,869 |

Small windows (4–64) make both streams *larger*: the record's order has id runs that sorting short windows breaks.

**Savings, channel 2: the metadata block on the real stack** (`M100k_*`: 2,800 pages of record ranks 100,000–102,799 with
the text stubbed, C4, round trips OK):

| order | archive B | metadata block B | page-id delta lines (bits/record) | other fields | title + stub lines |
|---|---:|---:|---:|---:|---:|
| record | 36,682 | 26,268 | 4,155 (11.87) | 22,113 | 10,185 |
| W = 1024 | 36,841 | 25,527 (−740) | 3,164 (9.04) | 22,363 (+251) | 11,084 (+899) |
| id-sorted (W = 2,800) | 36,790 | 25,152 (−1,116) | 2,709 (7.74) | 22,443 (+331) | 11,409 (+1,224) |

Sorting saves 2.83 bits per page id at W = 1024 and gives 0.72 back on the other fields (a record no longer follows a
similar article's record: fewer same-editor, same-minute neighbours). Net **−2.11 bits per record = −45 KB over the
172,277 articles [ESTIMATE]**; PPMD alone would have promised −5.6 bits (the real stack already codes the record's
deltas well). The title lines' +899 B is an artefact of stubbing as far as its size goes (titles adjacent), but it is
the same text loss §5.1's third table prices.

**Loss: the text.** PPMD-only on 100 MB windows of the record's order (`x3c_ppmd_windows.sh`), text region only:

| window (100 MB from record rank) | pages | PPMD text, record order | W = 256 | W = 1024 | W = 4096 | id-sorted (W ≈ 14 K) |
|---|---:|---:|---:|---:|---:|---:|
| B: rank 60,000 | 14,251 | 19,125,956 | +3,022 (+0.016%); W = 512: +7,131 (+0.037%) | +16,738 (+0.088%) | +44,117 (+0.231%) | +103,247 (+0.540%) |
| C: rank 110,000 | 20,081 | 10,066,809 | +6,013 (+0.060%) | +12,807 (+0.127%) | | |
| D: rank 5,000 | 14,980 | 18,146,434 | +4,869 (+0.027%) | +15,222 (+0.084%) | | |
| E: rank 140,000 to the end (32,277 articles + the redirects) | 103,425 | 4,892,043 | | +5,474 (+0.112%) | | |

(PPMD's metadata block in window B: 216,070 → 211,105 / 206,125 / 201,524 / 197,195; interlanguage unchanged within 0.3 KB.)

**Full-enwik9 ESTIMATE.** Text loss: PPMD bytes lost per article in each window, applied to the rank strata the windows
sample (W = 1024: 1.17 / 1.02 B per article in windows B / D for ranks 0–100 K, 0.64 in C for 100–140 K, 0.17 in E for
the rest: 141 KB of PPMD; W = 256: 0.21 / 0.33 in B / D, 0.30 in C: 40 KB), times β 0.34–0.59. Block saving: the real
W = 1024 proxy (2.11 bits per record at rank 100 K: −45 KB), up to −62 KB if it scales with PPMD's per-window block saving
(0.47–0.86 B per record across C, B, D, E against 0.51 where the proxy sits); other W by PPMD's block ratios. Order
stream: measured with cmix. The intercept of §4.2 (0 to about +60 KB of extra loss) is **not** in the table.

| W | order stream | metadata block | text loss | **net [ESTIMATE]** |
|---|---:|---:|---:|---:|
| 256 | −36.9 KB | −23 to −31 KB | +14 to +24 KB | **+36 to +54 KB** |
| 1024 | −70.6 KB | −45 to −62 KB | +48 to +83 KB | **+33 to +85 KB** |
| 4096 | −101.4 KB | −66 to −91 KB | +119 to +207 KB (window B only) | **−40 to +73 KB** |
| ≈ 14 K | −130.7 KB (W = 16384) | −86 to −118 KB | +279 to +484 KB (window B only) | **−30 to −270 KB** |

**Reading.** The loss grows faster than the savings: W = 256–1024 is the useful range, W ≥ 4096 is not. In that range K2 is
worth **about +45 KB [ESTIMATE]** if an order change costs the stack only through PPMD, and 0–60 KB less if the
non-PPMD loss seen on stub pages carries over to real text. W = 256 risks least (text loss 14–24 KB). It costs nothing
to build: no code, no RAM, no time; the order file is regenerated (`x3c_orders.py 256`, then P1's rank-delta) and the
decoder is unchanged. **It is not a GO as it stands**: the estimate straddles the 40 KB bar, its dominant uncertainty
cannot be resolved with ≤ 10 MB real runs, and its larger half (the order stream) disappears if X3a's rule-generated
order replaces the shipped stream (then: block −23 to −62 KB against text +14 to +83 KB, about zero).

### 5.2 K1, identity order

- Savings: 181,132 B (order stream) + 216 KB (page ids at about 1 bit) = **397 KB**, plus the reader's code.
- Loss from the coarse order alone, a lower bound: the first 100 MB of pages (12,347 pages) cost PPMD **19,279,406 B of
  text in id order and 19,182,710 B in record order (−0.50%)**, although that set holds only a tenth of each article's
  neighbours. At full scale that is 260–450 KB (β 0.34–0.59) before the denser neighbourhoods of the real file.
- Loss from the fine and middle scales at full density: id-sorting one 14 K-article window already costs +0.54% of PPMD
  text (279–484 KB at full scale for β 0.34–0.59), with savings of only 217–249 KB at that W.
- Id order is not a random order (15% of id-adjacent pairs are within 10 record ranks), which is why the loss is not
  larger; but the two partial losses are each about as large as the whole saving, and they add (540–930 KB against
  397 KB). **NO.** Not measured: the identity-order cost at full scale (§4.3), so this is an estimate from 100 MB
  windows, not a measurement.

### 5.3 K6, seriation by shared text

Greedy nearest-neighbour chain over MinHash (96 hashes) Jaccard of word 4-gram shingles on window B's 14,251 pages
(`x3c_seriate.py`; mean adjacent Jaccard 0.076 → 0.129, 538 chain restarts): PPMD text **19,218,075 B vs 19,125,956 B in
record order (+0.48%)**, metadata block +16,150 B. The embedding order is better for PPMD than a text-overlap chain: a
greedy chain strands its leftovers, and what the history has to offer an article is topic vocabulary from many
neighbours rather than one near-duplicate. A local variant (the chain may only pick among the next 64 unvisited pages
of the record's order): text 19,132,092 B (**+0.03%**), metadata block +12,781 B. Also worse: the record's fine order is not improved by
re-chaining on text overlap either.

## 6. Estimate and verdict

| candidate | full-enwik9 net of side information [ESTIMATE] | exe bytes | RAM | time | support | verdict |
|---|---:|---:|---:|---:|---|---|
| K2 windowed id-sort, W = 256–1024 | **about −45 KB of S, range +25 to −85 KB** (order stream −37 to −71 KB measured; block −23 to −62 KB; text +14 to +83 KB; unresolved 0 to +60 KB) | 0 | 0 | 0 | order stream: real coder. Block: real stack, one cold 2,800-record proxy. Text: PPMD-only × β, slope validated (r 0.87–0.99), intercept not | **lead, not a GO** |
| K1 identity order | **about +150 to +530 KB worse** (saves 397 KB, loses an estimated 540–930 KB) | −0.5 KB ×2 | 0 | 0 | PPMD-only on 100 MB windows × β; not measured at full scale | **NO** |
| K6 text-overlap seriation | worse than the record's order by 0.03–0.48% of PPMD text on a 100 MB window | — | — | — | PPMD-only | **NO** |
| K3, K5, K7, K8, K12, K13, K14 | ≤ 0 to 35 KB each, see §3 | | | | counted | **NO** |

**Verdict for the dart: NO-GO.** Nothing here reaches 40 KB with support, and nothing approaches 100 KB. What the dart
establishes instead:

- The record's article order is **not** a leftover from the CM era: against identity order it is worth an estimated
  540–930 KB to this stack and costs 397 KB; against a text-overlap seriation it wins by 0.03–0.48% of PPMD text. Its
  *fine* structure (below about a thousand articles) is worth less than the bits spent on it, which is K2's margin of
  about 45 KB.
- **So the arrangement is within about 50 KB (the K2 margin) of what the tested re-arrangements can extract.** The
  untested members (a cost-aware TSP, cluster-level leaf ordering, link-graph orders) would need full-scale PPMD-only
  runs on an idle box to price; the measured loss curve says their room is the coarse scale, where the record's order
  already earns the most.
- The 397 KB the permutation costs is the real prize in this area, and it belongs to whoever removes one of its two
  copies (X3a's rule for the comp9 copy; nothing found for the archive copy: K5, K12, K13).

## 7. Side findings

1. **A latent, order-dependent crash in X2 (and a silent loss in its redirect law).** `x2_timelaw.h` (`end_line`) accepts a
   page-id delta only when the very next line is `revision>`. Pages with a `<restrictions>` element put a
   `restrictions>…` line in between, so their delta is dropped and the tracked page id drifts by that amount
   (`x3c_x2drift.py` replays the logic). `try_rpid()` then reads `used_[prev_pid_ + 1]` with no lower bound.
   - Full run, record order (what C3 coded and C4 would code): **1,013 dropped deltas, final drift +2,740,978; all
     70,571 `try_rpid` calls use a drifted base** (the "next unused id" bitmap is misaligned, so the redirect law works
     as a plain small-delta prior). The drift never went below zero there, so C3 did not crash.
   - Record-order sample of the first 1 MB of pages (`A1_rec`): tracked id −297 at record 50 → **SIGSEGV at the same
     address in two runs** (`cmix_costlog+0x34b7d`, a `vector<bool>` read with index −296; ledger rows with exit 139).
   - Consequence: **any change of article order changes the drift path and can segfault the compressor in the metadata
     block, i.e. at about 95% of a full run.** That includes K2 and X3a's order work. Fix before the next full run: skip
     `restrictions>` (keep the pending delta across it) and bound `x >= 0`. The fix changes X2's predictions, so it needs
     its own round trip; the redirect law may then recover part of its projected 7 KB.
2. **"Each article is a fresh context" does not make the transformer order-invariant** (§2): it follows its PPMD prior at
   0.40 B per PPMD byte. Slice results therefore understate the transformer in a full run: the same articles cost the
   transformer alone 12.4% less there. This also re-opens PPMD-side ideas that were filed as CM-side (anything that
   improves PPMD's prediction is amplified through the transformer, not diluted by it).
3. **The permutation is paid twice** (§1): 181 KB in comp9 and 224 KB in the archive's page-id deltas, together 18.8 bits
   per article where log2(N!)/N is 16.0. Any design that derives one copy from the other pays it once. The decoder
   cannot derive it (it has no content before decoding); the compressor can (X3a's rule).
4. **No curriculum effect** (K7): G is flat from the first MiB; the online learners are mature within the first megabyte.
5. **Operational:** several concurrent runs that keep PPMD's `ppm.temp` on btrfs (the default for every cmix run) can
   stall the box on writeback; a 100 MB PPMD-only run needs its model file on tmpfs (`x3c_ppmdonly.sh`). Three slots of
   5.6 GB plus anything else of a few GB overcommits 30 GiB with swap full: two of my runs were OOM-killed
   (ledger rows with exit 137).

## 8. Runs and ledger rows

Real-stack runs (method `fx2T+C4-full-costlog (X3c order sample)`, C4 costlog twin in slice mode, archive identical to the
C4 submission binary; all timings contended):

| sample | pages | order | archive B | round trip | note |
|---|---:|---|---:|---|---|
| `A1_id` | 172 (first 1 MB of pages) | id | 129,729 | OK | |
| `A1_rec` | 172 | record rank | — | FAIL, exit 139, twice | the X2 crash of §7.1 (the second row's note blames memory pressure; that was wrong) |
| `A10_rec`, `A10_id` | 1,327 (first 10 MB) | record rank, id | — | FAIL, exit 137 | OOM-killed (§7.5); not rerun: `A10_rec` would hit the X2 crash |
| `M100k_rec` | 2,800 stub pages, ranks 100,000–102,799 | record | 36,682 | OK | |
| `M100k_w1024` | 2,800 | W = 1024 | 36,841 | OK | |
| `M100k_id` | 2,800 | id-sorted | 36,790 | OK | |
| `R60000_1_rec` | 119 (1 MB from rank 60,000) | record | 134,891 | OK | |
| `R60000_1_id` | 119 | id-sorted | 135,020 | OK | +129 B |
| `B10_rec` | 1,518 (10 MB from rank 60,000) | record | 1,283,771 | OK | 1.0253 bpb cold; the same articles cost 8.6% less in the full run |

PPMD-only runs (`cmix --ppmd-only`, no archive kept, no ledger rows): A100 id / rec; B100 rec / w256 / w512 / w1024 / w4096 /
id / ser / serL64; C100 rec / w256 / w1024; D100 rec / w256 / w1024; E100 rec / w1024. Totals in
`build/exp/X3c/ppmd/*.size`. Order-stream sizes: `build/exp/X3c/orders/order_w*.rd.size`.

Not verified, stated plainly: the identity-order cost at full scale; the real-stack text loss of K2 on a sample large
enough to resolve it; the block saving of K2 anywhere but ranks 100,000–102,799 and at W other than 1024 and 2,800;
that a build with a K2 order file round-trips in full mode (it should: the decoder sorts by id; but see §7.1).

