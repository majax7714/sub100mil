# X3a: provenance dart — how the artifact was made (order file, metadata block, relocated fields)

**Verdict: GO, for the article order file.** The lens was provenance: treat enwik9 and our own build inputs as
artifacts made by software, and ask what each producer computed deterministically. Twenty-two candidates were counted
on full enwik9 against the C3 full-run cost dump. The stream-side ones (metadata block, redirects, comments,
interlanguage block, bot tables) are **mined out**: together about 60 KB of oracle-gated gain in pieces of 1–25 KB
each, all under the bar. The hit is not in the archive at all. It is **comp9's article-order block (181,132 B, counted once in S; the gap
to the prize line is about 330 KB)**: the order file is not an arbitrary permutation but the output of a documented pipeline
(embedding → 1-D t-SNE → k-means on the axis → descending article number inside a cluster → a stable sort that moves
five page classes to the end), so it can be shipped as a **content rule plus a residual**. Built, smoke-tested and checked here:
the rule and a residual of **74,675 B** once CM-coded (§3.3) reproduce the real `.main_reordered` (999,988,851 B) **byte for byte**, so the
coded stream and the archive do not change and no full run is needed to price it.

**S effect [M]: order block 181,132 → 74,675 B (−106,457); packed exe 200,960 → 206,560 B (+5,600 per copy, two
copies). comp9 3,384,847 → 3,283,990 B. Net −95,257 B of S against C4.** No change to
the archive, to RAM at the coding peak, or to the decompressor's time; +22 s in the compressor.

Classification: **size / packaging (compressor side-information), non-transformer.** The idea of an order rule plus
residual is cmix-neif's (its README §4.5 and `order_codec.h`; listed in docs/LANDSCAPE.md as "−100 KB of S" but never
ported). neif applies it to a *modified* order; this is an independent implementation for the **unmodified** fx2 order.

Labels: **[M]** measured here, **[ESTIMATE]** derived, **UPPER BOUND** a ceiling (oracle-gated: the specialist is
only credited where it beats C3, with no mixing loss).

## 0. Method and reproduction

1. `x3a_decode` streams the C3 full-run cost dump (`build/exp/FULL/cost_full.bin`, 586,459,020 records) and inverts
   the WRT transform: `full.txt` (934,220,408 B, the reordered phda9 stream C3 really coded) and `full.cost` (float32
   per text byte; a coded byte's cost sits on the first text byte it produces). Sum of costs 93,313,033 B [M].
   25 s, dictionary-sized RAM. These are **C3's final costs** (after E12 and X2), so every "stake" below is what C3
   still pays.
2. `x3a_pages.py` (one row per page in coding order: title, minor, comment, redirect target, bits on each) and
   `x3a_meta.py` (one row per metadata record with per-field bits). Page k owns record k (243,425 each) [M].
3. `x3a_survey.py` counts the stream-side candidates. The metadata laws were priced by the snippets in §2 (kept in
   the attic notes).
4. `x3a_order.cpp` is the order-rule simulator with all parameters exposed; `patches/exp/X3a-order-rule/` is the
   build form (fixed parameters).

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic; X=build/exp/X3a; mkdir -p $X
g++ -O2 -march=x86-64-v3 -o $X/x3a_decode scripts/analysis/x3a_decode.cpp
$X/x3a_decode build/exp/FULL/cost_full.bin $D $X/full.txt $X/full.cost
python3 -P -E scripts/analysis/x3a_meta.py --tsv $X/meta_records.tsv
python3 -P -E scripts/analysis/x3a_pages.py
python3 -P -E scripts/analysis/x3a_survey.py
# order rule: simulator (parameters as k=v), then the build form against the real -e intermediates
g++ -O2 -march=x86-64-v3 -o $X/x3a_order scripts/analysis/x3a_order.cpp
O=build/refs/fx2-cmix-transformer-v1/tree/src/readalike_prepr/data/new_article_order
$X/x3a_order enc data/raw/enwik9 $O $X/final.res dfmax=20000 rec1=6 rec2=4 win=32 cls=1
g++ -O2 -march=x86-64-v3 -o $X/order_rule_enc patches/exp/X3a-order-rule/order_rule_enc.cpp
F=build/exp/E12/full      # .main / .main_reordered of a real comp9 -e run
$X/order_rule_enc enc   $F/.main $F/.new_article_order $X/order_main.res
$X/order_rule_enc check $F/.main $X/order_main.res $F/.main_reordered
scripts/slot.sh --fast scripts/analysis/x3a_cmsize.sh $X/order_main.res     # CM-coded size, as the construct step
scripts/slot.sh patches/exp/X3a-order-rule/build.sh                         # packed exe + comp_order
scripts/slot.sh patches/exp/X3a-order-rule/smoke.sh build/exp/X3a/smoke 1   # comp9 -e up to the coder, identity checks
```

## 1. Survey: 22 candidates, counted on full enwik9, priced with C3's real costs

Where the bits of the non-article parts are [M]: main stream 90,535 KB; metadata block 1,411.0 KB; interlanguage block
1,365.1 KB. Titles 553.1 KB (18.2 bits each), edit comments 556.7 KB (190,325 pages, 23.4 bits each).

| # | candidate (provenance law) | slots (full) | C3 pays per slot | **bits at stake** | removable | status |
|---|---|---:|---:|---:|---|---|
| 1 | **comp9 order block = rule(text) + residual** (the order file was made by a deterministic pipeline, §3) | 172,277 articles | 8.41 bits | **181.1 KB** (×1 in S) | 59% removed: **74,675 B [M]** | **GO, built (§3)** |
| 2 | comp9's dictionary copy derived from enwik9 (word set = frequent words; only the order needs hints) | 44,515 words | 17.9 bits | 99.9 KB (×1: the compressor's copy) | set membership costs 10.0 KB, the order ≤ 77.9 KB as a plain permutation [M]; neif ships 8.4 bits/word | untested lead, 12–50 KB [ESTIMATE] |
| 3 | order: sort the five tail classes by page id instead (neif's modification) | 58,352 articles | 4.3 bits of residual | ≈ 31.5 KB of residual + ≈ 2.7 KB header | all, but **the stream changes** | untested lead (§3.4) |
| 4 | metadata: revision id of pages edited after the 2005 renumbering, from pages with the same edit comment | 155,031 | 22.5 bits | 436.3 KB | 24.0 KB UPPER BOUND (61,821 slots have an exact-comment predecessor; only 12,840 within 100 ids) | NO-GO |
| 5 | metadata: renumbered revision id from page-id neighbours (renumbering is in page order: 99.5% monotone) | 26,720 (reordered part) | 7.48 bits | 25.0 KB | 10.9 KB UPPER BOUND (bracket 17.7 vs 23.7 KB on 26,115 slots) | NO-GO alone; a 6–10 KB add-on to X2 |
| 6 | metadata: timestamp of a renumbered revision from page-id neighbours (creation stratigraphy) | 88,394 | 16.8 bits | 185.4 KB | 2.5 KB UPPER BOUND; only 119 of 33,959 redirect brackets are under 10 days | NO-GO |
| 7 | metadata: redirect timestamps as small gaps after the previous redirect (id order) | 17,960 gaps ≤ 600 s | 6.6–10.6 bits | 19.1 KB | ≈ 3 KB (C3 already codes small gaps) | NO-GO |
| 8 | metadata: timestamp after X2 (rev ≥ 18.1M) | 155,031 | 4.85 bits | 94.0 KB | X2's residual; no further law found | covered (X2) |
| 9 | metadata: page-id delta as rank among unused ids (P1's order trick, in the archive) | 172,277 | 10.38 bits | 223.6 KB | 9.1 KB [ESTIMATE] (7.69 → 7.27 bits of log-distance) | NO-GO |
| 10 | metadata: page id from the title's sort position (conversion script and rambot created pages alphabetically) | ≈ 45K census + ≈ 25K early pages | 5.4–9.7 bits | ≈ 60 KB | early ids are only clustered by first letter (53% adjacent-ascending); census 78–84% ascending but already 5.4–6.2 bits | NO-GO, ≤ 15 KB [ESTIMATE] |
| 11 | metadata: user name from the edit comment (bots have fixed comments) | 101,060 with an exact-comment predecessor | — | 99.7 KB on those | hits (65,687) pay only 25.1 KB | NO-GO |
| 12 | metadata: contributor from revision-id neighbours (bot runs, sessions) | 69,198 hits | 2.0 bits | 17.7 KB | already cheap | NO-GO |
| 13 | metadata: user name → user id (functional dependency) | 185,891 repeats | 0.013 bits | **0.3 KB** | the warm CM has it; 19,506 first uses pay 47.5 KB and are not derivable | NO-GO (confirms X2 at full scale) |
| 14 | metadata: repeated IP addresses | 11,751 repeats | 10.0 bits | 14.7 KB | ≈ 5 KB | NO-GO |
| 15 | redirect target is an already-coded article title (referential integrity) | 71,067 | 5.6 bits | 49.8 KB | 90% are; a 172K-title index would cost 17.4 bits | NO-GO |
| 16 | redirect title was seen earlier as an unresolved link target | 35,384 of 71,148 | 20.2 bits | 89.4 KB | candidate set is 1.5M link targets = 20.5 bits uniform; needs a > 50 MB table (RAM headroom 10 MB) | NO-GO |
| 17 | section comment `/* X */` → the article's heading `==X==` (comment is coded before the text) | 30,118 matches | 3.3 bits near, 7.4 bits beyond 3,000 B | 19.7 KB (13.0 KB far) | ≈ 8 KB | NO-GO |
| 18 | move comments (`A moved to B`) / revert comments | 44,000 / 10,558 | 18.9 / 50.2 bits | 104.0 / 66.2 KB | the target and the reverted user are new information | NO-GO |
| 19 | interlanguage language codes from `robot Adding: xx` comments | 6,667 pages, ≈ 8,651 codes | 1.92 bits per code | 162.9 KB on all codes | ≤ 3 KB | NO-GO |
| 20 | interlanguage group length from the article's size (via T1's group → page map) | 680,440 links | 0.67 bits on `]`/newline/`</text>` | 56.6 KB | untested, ≤ 10 KB [ESTIMATE] | not tested |
| 21 | year / decade / century navigation tables (arithmetic of the title) | 1,844 + 1,814 rows | 1.7 bits per row | **0.8 KB** | the transformer already does it | NO-GO |
| 22 | contributor or bot identity as a latent variable for the article body, minor flag (98,816 lines, 14.5 KB) | 243,425 | — | — | not decoder-valid: the contributor is coded after every body; moving it is a stream change for a frozen transformer | not priced |

Small fields: `restrictions` 1,013 records, 1.0 KB. Tags of the block cost 0.4 KB in all.

**Reading.** X2 and T1 took what was exactly computable in the relocated blocks. What is left there is information:
the last-edit time of 155K pages (436 KB at 22.5 bits each, against 24.5 bits for a uniform draw over the id range),
first uses of user names and ids, IP addresses, the permutation channel. The warm, reordered CM already has every
copy-type dependency (user → id at 0.013 bits, same-bot neighbours, small time gaps), because the reorder puts the
pages of one bot run next to each other. The only place where a deterministic producer's output was still being paid
for in full was our own side-information.

## 2. Metadata block after C3, per field [M]

`x3a_meta.py`, 243,425 records, 24,698,771 B of text, **1,411.0 KB** in C3:

| field | n | C3 KB | bits each |
|---|---:|---:|---:|
| revision id | 243,425 | 473.7 | 15.57 (22.5 above 18.1M, 3.4 below) |
| timestamp | 243,425 | 279.4 | 9.18 (4.85 above 18.1M with X2, 16.8 below) |
| page-id delta | 243,425 | 251.1 | 8.25 (10.38 in the reordered part, 3.1 in the redirect tail) |
| user name | 205,397 | 241.1 | 9.39 |
| IP | 38,028 | 116.5 | 24.52 |
| user id | 205,397 | 47.7 | 1.86 (first use 19.5, repeat 0.013) |
| restrictions | 1,013 | 1.0 | 7.91 |

X2's timestamp slots now cost 4.85 bits (X2 projected 4.3–7.5 with the specialist): **X2 delivers at full scale**.
The renumbered half (rev < 18.1M) keeps its 16.8 bits: 39% of the id-ordered redirects are *older* than their
predecessor and 27% more than a day newer, so creation order does not date the last edit.

## 3. The order file as rule plus residual

### 3.1 Provenance [M]

`third_party/fx2-cmix-transformer-v1/src/article_order/*.ipynb` documents how `new_article_order` was made:
`embeddings.ipynb` (title + first 4,000 characters, voyage-large-2-instruct) → `tsne.ipynb` (t-SNE to **one**
component) → `kmeans.ipynb` (k-means on that axis, N/80 clusters; the clusters in axis order, article number inside a
cluster, later reversed) → `manual-sort.ipynb` (a **stable** sort that moves census, image, disambiguation, geodis and
hndis pages, detected by fixed strings, behind everything else, in that order).

The file confirms it: 172,277 article numbers, **97.8% of the steps descend**, 3,800 maximal descending runs of at most
162 articles (median 55–87 in the first 150K positions; the image/disambiguation tail has runs of median 3–4 because
those pages were pulled out of clusters all along the axis). And the class rule is exact: with the notebook's detection
strings, the five class boundaries can be placed so that **0 of 172,277 articles disagree** with the file.

So the permutation is (a) the cluster of each article, (b) the order of the clusters on the axis, (c) the page class.
(c) is a function of the text. (a) is "which articles are similar", which the compressor can estimate from the text.
P1's rank-delta list pays 8.41 bits per article for it.

### 3.2 The rule

Articles are visited in page order. Each has hashed features: title tokens, links and words (≥ 5 letters) of the
first 600 characters, categories, template names, the first 200 links. A feature votes with an integer IDF for the
cluster of each of the last 32 earlier articles that carry it; a cluster's vote is smeared onto its 8 axis neighbours
per side, divided by (cluster size + 16), and the last two clusters used by the same page class get a bonus. Only
clusters of the article's own class are candidates. The residual holds, per article and class by class, 0 (opens a
cluster), 1 + rank of its cluster in the vote, or 4096 + cluster number; plus the axis order of the clusters (3,801 numbers) and the 38 pages
where "is in the order file" differs from the redirect prefix test. Integer arithmetic only (the IDF is a Q8 log2
from a table built with integer squarings); feature ids are assigned in order of first appearance, so nothing depends
on hash-map iteration order. Encoder and decoder share `order_rule.h`.

Rank statistics [M]: rank 0 for 84,244 articles (49%), rank 1 for 23,424, ranks 2–3 for 19,330, 4–15 for 25,064,
16–63 for 10,206, ≥ 64 for 5,412, escapes 796, new clusters 3,801.

| variant (order-0 adaptive estimate of the symbols) | bits / article | B |
|---|---:|---:|
| P1 rank-delta list, cmix-coded (the baseline) | 8.41 | 181,132 |
| neif's parameters (df ≤ 175, all earlier articles vote) | 4.26 | 91,797 |
| df ≤ 400 | 4.12 | 88,628 |
| any df, last 64 postings vote | 3.65 | 78,560 |
| any df, last 32 postings, stronger recency bonus | 3.64 | 78,301 |
| **+ page classes restrict the candidates (chosen)** | **3.46** | **74,598** |

No axis smoothing costs +7.4 KB; lead of 1,200 or 4,000 characters, other normalisations and bonuses are within
±1.5 KB. By page class (static order-0 over the chosen residual, 73.9 KB) [M]:

| class | articles | bits / article | KB | clusters opened | rank 0 |
|---|---:|---:|---:|---:|---:|
| normal | 113,925 | 2.99 | 42.5 | 1,697 | 55% |
| census | 36,519 | 3.70 | 16.9 | 547 | 37% |
| image | 13,908 | 3.71 | 6.5 | 450 | 52% |
| disambiguation | 7,448 | 8.43 | 7.8 | 1,018 | 13% |
| geodis / hndis | 320 / 157 | 3.84 / 5.29 | 0.3 | 40 / 49 | |

### 3.3 Exactness and size [M]

- `order_rule_enc enc` on the real `.main` (243,425 pages) and the fx2 order file: 393,027 B of residual text;
  the decoder returns the 172,277 positions that `article_reorder.h` computes (and the independent X2 mirror
  `build/exp/X2/meta_pages.tsv`).
- `order_rule_enc check`: rule + residual, then the unused pages in id order, written as `reorder()` writes them:
  **999,988,851 bytes identical to the real `.main_reordered`** of a comp9 -e run (`build/exp/E12/full`). So phda9,
  WRT, the coded stream and archive9 are untouched.
- CM-coded like the construct step codes the order block (`env -i ./cmix_orig -c`, packed C3-full+P1 exe):

| file | text B | CM-coded B | |
|---|---:|---:|---|
| P1 rank-delta list (`new_article_order.rd`), the C4 order block | 742,217 | **181,132** | [M] reproduces C4's `comp_order` exactly, so the method is the build's |
| residual, earlier variant (any df, last 64 postings, no classes), text | 396,804 | **80,625** | [M] order-0 estimate + header was 78,560 + ≈ 5,050 |
| same, LEB128 varints instead of text | 185,093 | 80,791 | [M] text is the better container for this CM |
| residual, chosen rule (classes), symbols in page order | 393,027 | **76,846** | [M] (the order-0 difference to the row above predicted 76,663); xz -9e 95,520 |
| **residual, chosen rule, symbols stored class by class (the patch)** | 393,027 | **74,675** | [M] −2,171 B for the layout alone (per-class order-0 predicted −2.2 KB); xz -9e 93,656 |

(`scripts/analysis/x3a_cmsize.sh`, fast lane; `build.sh` produces the same block as `comp_order`.)

- Time: 54 s for encode + verify (build time), **21.6 s for the decode** the compressor runs, single core, contended
  [M]. Peak RSS of the decode 402 MB [M], transient and before the model is allocated; in the patch it runs before
  `loadFile(".main")`, so the 1 GB line buffer reuses the memory. The decompressor does not run it.

### 3.4 Cost and estimate

`patches/exp/X3a-order-rule/build.sh` (C4 tree + the patch, same PGO pipeline and guards as C4's build, P1 stub) [M]:

| item | C4 | X3a | effect on S |
|---|---:|---:|---:|
| order block (`comp_order`, ×1) | 181,132 | **74,675** | **−106,457** |
| exe, raw | 537,888 | 546,240 | |
| exe, packed (×2: comp9 and archive9) | 200,960 | **206,560** | **+11,200** |
| comp9 (exe + dict 99,889 + order + weights 2,902,850 + 16) | 3,384,847 | **3,283,990** | |
| archive9 payload | — | unchanged (`.main_reordered` identical) | 0 |
| **S** | | | **−95,257 B** |

- The header costs 8,352 B raw and 5,600 B packed per copy, net of the removed remap loop and rank-delta reader.
  It packs worse than average code (0.67 against the exe's 0.37). Trimming it (the nine census strings, one sort
  instantiation, the escape path) might return 1–2 KB per copy; not tried.
- RAM: 402 MB transient in the compressor's preprocessing, before the model exists; nothing at the coding peak
  (smoke below). Time: +22 s compress (contended), 0 decompress.
- Nothing here depends on a full run: the block is a file we build, and the stream is unchanged. The estimate has
  no range for that reason; what remains is a correctness risk, covered by the checks below.

**Checks of the real build [M]:**
- `smoke.sh` (the packed comp9 of 3,283,990 B, `-e enwik9` under `env -i`, killed one minute after the coder
  started; `build/exp/X3a/smoke/smoke.log`): the stub self-extracts the dictionary and the residual (393,027 B,
  identical to the build's), `reorder()` rebuilds the order, and **`.main_reordered` (999,988,851 B), `.ready4cmix`
  (934,220,400 B) and `.dict` are byte-identical to the record's -e run**; E12, X2, T1 and G1 come up on `.dict` with
  the coded block header. Session RSS: 0.39 GB during the order decode, 0.20 GB one minute later (so the rule's
  memory is returned), 5.53 GB at coder start and 5.70 GB a minute later (C4's smoke: 5.81 GB after 3 minutes;
  one-minute sampling, so only a coarse check).
- Slice mode never reads the order, so a slice bench only shows that the rebuilt exe codes like C4's:
  `enwik9_1M` **126,132 B = C4's**, round trip OK (ledger `fx2T+C4+X3a-order-rule`; comp 143 s, decomp 160 s,
  contended; peak RSS 5,694 MB; 1 thread).
- Not done: a full run (not needed for the size; the next full run should carry this build and confirm archive9
  restores enwik9 as before, which it does by sorting on page ids, unchanged code).

**Relation to X3c** (running in parallel): X3c tests *changing* the order (id-sorted windows) to shrink the order block
and the page-id deltas at a payload cost. The two compete for the same 181 KB; this one leaves the order, and
therefore the payload, exactly as they are. The rule also codes any order made of descending runs, so it would apply
to an X3c order too, with a different residual.

**Leads on the same block, untested:**
- Tail classes by page id (row 3): neif sorts census, image and the three disambiguation classes by descending page
  id, which needs no residual for 58,352 articles (≈ 31.5 KB of the residual here, plus ≈ 2.7 KB of cluster header)
  and gave neif −12 KB of payload. It changes the coded order of 34% of the articles, so its payload effect needs a
  proxy bench (the tail pages in both orders) or the next full run.
- Anchoring the tail runs to the normal cluster they were pulled from (the stable sort keeps their axis order), so
  normal neighbours vote for them: aimed at the disambiguation and image classes (14.3 KB), perhaps −4 to −6 KB.
- Coding the residual with previous-rank contexts instead of as plain text (the class split is already in).

## 4. Verdict

| | |
|---|---|
| **GO: order rule + residual** | **−95,257 B of S [M]**: order block −106,457, packed exe +5,600 × 2; stream and archive unchanged |
| NO-GO: metadata, redirect, comment, interlanguage and bot-table laws (rows 4–21) | about 60 KB of oracle-gated gain in 14 pieces, the largest 24 KB; attic/2026-11-11-X3a-provenance-laws |
| best untested lead | the dictionary copy in comp9 (row 2: 99.9 KB at stake, word set derivable for 10 KB) and the tail-class order (row 3) |
