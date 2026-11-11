# X3c: the arrangement dart (article order, block order): NO-GO for the 100 KB hunt

Full write-up, tables and reproduction: [docs/design/X3c-arrangement.md](../../docs/design/X3c-arrangement.md).
Code (kept in place): `scripts/analysis/x3c_*`. Scratch: `build/exp/X3c/` (gitignored). Commits: `git log -- scripts/analysis/x3c_* docs/design/X3c-arrangement.md`.

## Idea

T1 and X2 both won from consequences of how the pipeline rearranges enwik9 (articles reordered by an embedding order
shipped in comp9; metadata and interlanguage links moved to blocks). Nobody had asked whether the arrangement itself
is right for a stack where a memorized transformer does most of the work. Lens: seriation, stratigraphy, genome
assembly, leaf ordering, TSP, library classification, columnar stores, curriculum design, caching, matrix reordering.
18 candidates counted (survey table in the design doc).

## What was measured

- The reorder costs about 397 KB of S before it earns anything: 181,132 B of order stream in comp9 plus about 216 KB
  because the archive's page-id deltas carry the same permutation again (10.38 bits per article instead of about 1).
- The "order-sensitivity bound" G = transformer-alone − final = 3,079,208 B is not a bound: the transformer takes PPMD's
  distribution as an input, and the same articles cost the transformer alone 12.4% less in the warm full run than in a
  cold slice. Per article, Δfinal = 0.34–0.59 × ΔPPMD-alone (r 0.87–0.99, five pairs).
- Proxy: the real PPMD component alone (`cmix --ppmd-only`) on 100 MB windows × β.

## Sub-ideas that failed

| idea | numbers | why |
|---|---|---|
| **K1 identity order** (drop the reorder) | saves 397 KB; PPMD text +0.50% for id vs record order on the first 100 MB of pages (260–450 KB at full scale with a tenth of the neighbours), +0.54% more when a single 14 K-article window is id-sorted (279–484 KB) | the record's order is worth more than it costs, at every scale above about a thousand articles |
| **K3 order = f(metadata)** (revision id, editor) | revision-adjacent pairs within 10 record ranks 7.3% (id order 15.0%, random 0.0%) | dominated by identity order, which already fails |
| **K5 order stream in archive9 + page-id law** | 165.4 KB saved, 181.1 KB shipped | −16 KB; X2's pricing stands with P1's stream |
| **K6 seriation by shared text** (MinHash 4-gram Jaccard, greedy chain) | PPMD text +0.48% vs the record's order, metadata +7.5%; local re-chaining (next 64 pages) +0.03% | a greedy overlap chain strands leftovers; the history's value is topic vocabulary from many neighbours, which the embedding order provides |
| **K7 curriculum** | G is 2.4–3.0% from the first MiB on | no learning curve to exploit |
| **K8 redirects sorted by target** | ≤ 150 KB on targets, ≈ +115 KB on their page ids | the permutation costs what it saves |
| **K12/K13 page id from alphabetical neighbour / last K records** | 13.55 bits vs 7.69 for the previous record; K = 64: 7.14 before log2 K | no law there |
| **K14 piece cuts** | 15 articles over 2^17 tokens | nothing at stake |
| lpaq-like CM as proxy (`x3c_proxy.cpp`) | 64 MB in 9 min at full scale under load, unvalidated | wrong channel (the stack follows PPMD) and too slow |
| full-scale PPMD-only runs | stopped at 3.5%: 14 GB model files on btrfs, ~1 GB/s writeback, two OOM kills | needs an idle box |

## The one lead: K2, windowed id-sort

Keep the record's coarse order, sort pages by id inside windows of 256–1024 articles. No code, no RAM, no time: only a
different order file. Measured pieces: order stream 181,132 → 144,186 (W = 256) / 110,527
(W = 1024) with cmix; metadata block −2.11 bits per record on the real stack at W = 1024 (−45 to −62 KB); text loss from
PPMD-only on four 100 MB windows × β: +14 to +24 KB (W = 256), +48 to +83 KB (W = 1024). **About +45 KB [ESTIMATE],
range −25 to +85.** Not a GO: an order loss that does not pass through PPMD (0.3–0.4 B per article on stub proxies,
0.67 ± 1.84 on the one real-text pair, i.e. 0–60 KB at full scale) cannot be resolved with ≤ 10 MB real runs, and the
order-stream half disappears if X3a's rule-generated order replaces the shipped stream. At W ≥ 4096 the loss overtakes
the savings.

## What we'd try differently

- Measure identity order and K2 at full scale with PPMD-only on an idle box (about 1.5 h per order, model file on disk
  and nothing else running), instead of 100 MB windows.
- The non-PPMD order loss (the intercept) needs a real-text pair of tens of thousands of articles, i.e. a 100 MB-class
  real run per order, to resolve to ±20 KB. A 10 MB pair (`B10_rec` exists; `B10_id` was cancelled) cannot.
- Queue discipline on the shared box: one real pair first, everything else after; never more anonymous memory than the
  three slots (two runs were OOM-killed), and no disk-backed PPMD model files beside other runs.
- A cost-aware order (TSP with the order stream and the page-id channel in the objective, K15) and cluster-level leaf
  ordering (K16) are the untested members of the family; both need full-scale PPMD-only pricing.
- Fix X2's page-id tracking (side finding 1 of the design doc) before any run with a changed order.
