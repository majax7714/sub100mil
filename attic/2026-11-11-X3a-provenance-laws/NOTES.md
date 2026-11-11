# X3a sub-results: provenance laws in the coded stream (NO-GO)

Part of X3a (docs/design/X3a-provenance.md), whose GO is the article-order rule (patches/exp/X3a-order-rule).
This note keeps the stream-side candidates that were counted and dropped. Date 2026-11-11, offline only, no
compressor change, no ledger rows. All numbers are C3's real full-run costs (`build/exp/FULL/cost_full.bin`
decoded by `scripts/analysis/x3a_decode.cpp`), full enwik9, warm and reordered.

## Idea

X2 (timestamps from revision ids) and T1 (interlanguage titles from the page title) worked because phda9 moved a
field away from what determines it. The lens here: every producer of the artifact (MediaWiki's id counters, the
2005 revision renumbering, bots, the dump tool, phda9) computed something deterministic, so look for more
functional dependencies between the relocated metadata / interlanguage blocks, the edit comment and the page.

## Numbers (bits at stake = what C3 pays on the slots; "gated" = oracle-gated upper bound, no mixing loss)

| law | slots | C3 pays | stake | removable |
|---|---:|---:|---:|---|
| revision id (≥ 18.1M) near the revision ids of earlier pages with the same edit comment | 61,821 with a predecessor | 21.5 bits | 166.5 KB | 24.0 KB gated; only 12,840 are within 100 ids, 25,276 within 1,000 |
| renumbered revision id inside the bracket of its page-id neighbours | 26,115 | 7.26 bits | 23.7 KB | 10.9 KB gated (uniform in the bracket: 17.7 KB) |
| renumbered timestamp inside the bracket of its page-id neighbours | 41,659 in a bracket | 15.3 bits | 79.5 KB | 2.5 KB gated: brackets are years wide (119 of 33,959 redirect brackets under 10 days) |
| redirect timestamp = previous redirect's + a small gap | 9,434 gaps ≤ 60 s / 8,526 ≤ 600 s | 6.59 / 10.63 bits | 19.1 KB | ≈ 3 KB: C3 already codes them near the ideal 5 / 9 bits |
| page-id delta as a rank among the still-unused ids | 172,277 | 10.38 bits | 223.6 KB | 9.1 KB (mean log-distance 7.69 → 7.27 bits) |
| page id from the title's sort position | — | — | ≈ 60 KB | early ids are clustered by first letter only (53% adjacent pairs ascending); census 78–84% but already 5.4–6.2 bits |
| user name = the user of earlier pages with the same comment | 65,687 hits | 3.1 bits | 25.1 KB | already cheap |
| contributor = a revision-id neighbour's (±2 neighbours) | 69,198 hits | 2.0 bits | 17.7 KB | already cheap |
| user id from user name | 185,891 repeats | 0.013 bits | 0.3 KB | nothing; first uses (19,506, 47.5 KB) are not derivable |
| IP seen before | 11,751 | 10.0 bits | 14.7 KB | ≈ 5 KB |
| redirect target is an already-coded title | 63,959 (90%) | 5.2 bits | 41.9 KB | no: an index into 172K titles is 17.4 bits |
| redirect title seen earlier as a link target | 35,384 (50%) | 20.2 bits | 89.4 KB | no: 1,496,205 distinct targets = 20.5 bits uniform, and the table is > 50 MB |
| `/* section */` comment → heading in the text | 16,101 near / 14,017 beyond 3,000 B | 3.33 / 7.41 bits | 19.7 KB | ≈ 8 KB |
| interlanguage codes named in `robot Adding:` comments | ≈ 8,651 codes | 1.92 bits | ≈ 2 KB | ≤ 3 KB |
| year / decade / century navigation rows | 1,844 / 1,814 | 1.7 bits a row | 0.8 KB | nothing |

## Why it failed

- The remaining metadata cost is information, not structure: the last-edit revision id of a page edited after
  mid-2005 costs 22.5 bits against 24.5 for a uniform draw over the id range, and nothing in the page dates its last
  edit. Renumbered revisions carry the *last* edit before the import, so creation order (page id) does not date them:
  39% of the id-ordered redirects are older than their predecessor.
- Every copy-type dependency is already cheap in the warm full run. The reorder puts the pages of one bot run and of
  one editing session next to each other, so the CM sees the neighbour's user, revision id and time directly. Slice
  numbers (cold, id order) overstate these laws; the full-run dump does not.
- Candidate sets that would beat the model are too large for the 10 MB of RAM headroom (link-target table).
- In-window structure (year tables, move comments followed by the redirect target) is done by the transformer:
  the navigation rows of 1,844 year articles cost 0.4 KB in all.

## What we'd try differently

- Fold the renumbered-revision-id bracket (≈ 6–10 KB) into X2's stage if that stage is reopened; it needs one more
  sorted map keyed by page id.
- The remaining large, exactly computable object is on the compressor side: comp9's dictionary copy (99.9 KB,
  counted once). The word *set* is derivable from enwik9's word frequencies for about 10 KB; the order is the hard
  part (≤ 77.9 KB as a plain permutation; neif ships 8.4 bits per word). See the design doc, row 2.
- Reproduce: `scripts/analysis/x3a_decode.cpp`, `x3a_meta.py`, `x3a_pages.py`, `x3a_survey.py` (commands in the
  design doc §0). The metadata laws were priced with short causal walks over `build/exp/X3a/meta_records.tsv`
  (pid = cumulative delta, a sorted list of already-seen keys, bracket or nearest-neighbour test per record).
