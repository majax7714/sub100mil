# X2 sub-results that failed: domain laws in article text, and the page-id-from-article-order law

Parent experiment: X2 (cross-domain dart, round 2), overall **GO** for the metadata-block time law. Full write-up,
tables and reproduction: [docs/design/X2-cross-domain.md](../../docs/design/X2-cross-domain.md). Code (kept in place):
`scripts/analysis/x2_census.py`, `x2_slots.py`, `x2_meta2.py` (field `pid`), `x2_sim.cpp`. Commits: see `git log -- scripts/analysis/x2_*`.

## 1. Domain laws in the article text (Track B, E12's lesson generalized)

**Idea.** Values that a law fixes exactly, outside E12's census arithmetic: ISBN/ISSN check digits, ordinal suffixes,
chronological lists (sorted years), weekdays and Roman numerals in year articles, day-of-year articles, ages from
birth/death years, sums/differences/percentages/ratios/conversions in table rows and infoboxes (Pld = W + D + L,
Pts = 3W + D, W–L percentage), percentages that sum to 100, election boxes, molar mass, Kepler, taxobox binomials.

**Results [M].** 30 candidates counted over full enwik9 with the record's bits per derivable span (42 MB cost-log
sample): together about 150 KB at stake. Largest: chronological lists 50 KB, ordinal suffixes 20.8 KB, ISBN-10 13.8 KB,
generic numeric laws about 35 KB net of a null control (coincidences are frequent). Causal E12-style sims:
10M +327 B (ISBN +168, chronology +170, ordinal −11); 1M +8 B. Projected about 14 KB [ESTIMATE].

**Why it failed.** The 2006 dump has little law-bound content outside the Rambot census: 41 chembox molar masses,
101 orbital fields, 121 year articles with Roman numerals, no India/Japan/Poland bot families yet. And where the law is
partial (sorted lists) or fixes only *which* suffix, not *whether* one follows (ordinals), the record already gets most of it.

**What we'd try differently.** Only ISBN is clean (74% of its bits removed), worth folding into E12 for about 10 KB if
the class is nearly free to add.

## 2. Page id from the article order (metadata block)

**Idea.** In the full run's reordered metadata block, page-id deltas jump; page ids grow with the order file's article
number, so a specialist pins them to 2.1 bits (the record pays 6.7–12.6). Simulated ≈ 165 KB.

**Why it failed.** Not decoder-valid. `archive9` doesn't carry `.new_article_order` (only comp9 does); the decoder
restores the order by sorting pages on page id, so the deltas *are* the permutation's channel. Using the law would
mean adding the 201 KB compressed order file to archive9.

**What we'd try differently.** Model the permutation in-stream better (deltas from decoded titles), or ship the order
file and code the block in id order (page-id delta 1.14 bits there). Needs a same-page-set comparison first; see the
design doc §7.
