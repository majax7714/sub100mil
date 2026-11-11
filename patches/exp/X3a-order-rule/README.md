# X3a-order-rule: the article order as a content rule plus a residual

comp9 carries the article order (C4: `comp_order` 181,132 B, P1's rank-delta list CM-coded; counted once in S).
The order file is the output of a documented pipeline (fx2-cmix `src/article_order/*.ipynb`: embedding → 1-D t-SNE →
k-means on the axis → descending article number inside a cluster → stable sort of five page classes to the end), so
most of it can be recomputed from the text. This patch makes `reorder()` rebuild the order from `.main` with a rule
and a small residual. **The order itself is unchanged**: `.main_reordered` is byte-identical, so the coded stream,
archive9 and every slice result are C4's. Design, survey and numbers: docs/design/X3a-provenance.md §3.
The scheme is cmix-neif's (order rule + residual; its `order_codec.h`), re-implemented for the unmodified fx2 order.

## Files

- `order_rule.h`: feature extraction, class rule, cluster vote and `ordrule::DecodeOrder(main, residual, positions)`.
  Integer arithmetic only. Goes to `src/readalike_prepr/order_rule.h`.
- `0001-X3a-order-rule.patch`: `article_reorder.h` on top of the C4 tree (`patches/exp/C4-combo/mktree.sh`): replaces
  the article-number remap and P1's rank-delta reader (`order_rankdelta.h` is no longer needed) by `DecodeOrder`,
  called before `loadFile(".main")`.
- `order_rule_enc.cpp`: build-time encoder (`enc MAIN ORDERFILE OUT.res`, verifies by decoding) and the exactness
  check (`check MAIN RES MAIN_REORDERED`: byte compare against a real `.main_reordered`).
- `build.sh`: C4 tree + this patch, PGO build, P1 stub, residual, CM-coded `comp_order`. Run through `scripts/slot.sh`.

The residual is "%d\n" text like the old order file: number of exception pages and their index deltas (pages where
"is in the order file" differs from the redirect prefix test: 38), number of class corrections and (delta, class)
pairs (0), number of clusters and the axis order of the clusters (3,801), then one symbol per article in page order
(0 = opens a cluster, 1 + rank in the vote among clusters of its class, 4096 + cluster).

## Status (2026-11-11)

- Exactness [measured]: `order_rule_enc check build/exp/E12/full/.main order_main.res build/exp/E12/full/.main_reordered`
  → 243,425 pages, 999,988,851 bytes identical.
- Sizes: see the design doc §3.3 / §3.4.
- **Not done:** a packed comp9 `-e` smoke (`patches/exp/C4-combo/smoke.sh` style) with the residual as the order
  block: self-extraction, `.main_reordered` identity inside the real binary, RSS at coder start. Required before a
  full run. Slice mode never reads the order, so slice benches say nothing about this patch.
