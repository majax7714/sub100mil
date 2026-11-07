# T1-fieldcopy: interlanguage title = English title (a field copy across phda9's block split)

Type: **model addition (non-transformer specialist, final stage)**. Base: `patches/exp/C3-combo` (C2 + X2), frozen
weights. Date: 2026-11-07. Survey, pricing, sims and projection: [docs/design/T1-fieldcopy.md](../../../docs/design/T1-fieldcopy.md).

## Mechanism

phda9 moves each article's trailing interlanguage links (from the first one to the end of the text, `</text>`
included) into one group of a block that follows the metadata block. The article keeps no `</text>`, so **the k-th
page without `</text>` (and without an empty `<text ... />`) owns the k-th group**, and every title is decoded
before the block starts. This holds in any page order, the full run's reordered one included.

`t1_fieldcopy.h` (copied into the tree as `src/models/t1_fieldcopy.h`) WRT-decodes the coded bytes with its own
small decoder (X2's), and:

- on each `    <title>T</title>` line keeps the **coded bytes of T** as that line coded them (and of T without a
  trailing ` (...)`); at `  </page>` it queues them if the page had no `</text>`;
- enters the interwiki block at the first `[` line after `/contributor>`; a rolling 7-char tail detects each
  group's `</text>` (groups can hold long moved lines, so it's not a line test);
- at every link title (after `[xx:` at a line start) opens a slot with candidates coded(T) + `]` (weight 1) and
  coded(T without parens) + `]` (0.3), followed bit by bit; the slot closes at the first byte that leaves every
  candidate or at the candidate's end;
- slot class: group history (first link / an earlier link of this group was T / none was) × the language's running
  rate of T links (Laplace; < 0.05, < 0.2, < 0.5, ≥ 0.5): 12 classes.

`FieldCopyMix` is X2's `TimeLawMix` with 12 classes and lr 0.004 (inputs stretch(p), bias, StateMap of p1,
stretch(p1); weight set per class × state × bit). The predictor applies it **after** X2's stage:
`p = e12->Predict(p); p = x2->Predict(p); p = t1->Predict(p)`, updated after X2. No tables, nothing in the archive.

## Files

- `t1_fieldcopy.h`: the model and the mixer (also used by `scripts/analysis/t1_csim.cpp`).
- `0001-T1-predictor-fieldcopy.patch`: `src/predictor.{h,cpp}` lines next to X2's in C3's predictor.
- `build.sh`: C3's build (steps 1–10) + this patch and header; PGO training with `T1_DICT` on. Output
  `build/exp/T1/bin/cmix`, or `$T1_OUT`.
- `run.sh <name> <slice|path> <notes> [--fast]`: C3's env plus `T1_DICT` (`T1=0` turns T1 off: the C3 control on the
  same binary). `FX2_BIN` overrides the binary.

Runtime switch: `T1_DICT=<english.dic>` (both directions; `T1_LR`, default 0.004). For a submission it becomes a
compile-time default reading the embedded dictionary, as for E12/X2.

## Results (round trips; ledger rows `fx2T+T1-v1`, control `fx2T+C3-v1`)

| input | C3 | C3 + T1 | gain | causal sim (`t1_csim`, record costs) |
|---|---:|---:|---:|---:|
| `enwik9_10M` | 1,261,700 | **1,261,120** | **−580 B (−0.046%)** | −586 B |
| interwiki proxy `build/exp/T1/iwp_120k.xml` (reordered ranks 120K+, 1,902 groups) | 33,864 | **32,805** | **−1,059 B (−3.13%)** | −1,065 B |
| `enwik9_1M` | 126,244 | see the design doc | | −10 B |

The 10M row was built from `af861bf` (header before the group-end fix in `ec64749`; the fix changes nothing on
10M: same slots, same −586 B in the csim); the proxy row from `ec64749` (`build/exp/T1/bin2/cmix`).

Cost: 0.04 µs per coded byte for the model over the whole 586 MB stream (24 s, `t1_csim stream=1`), plus the
mixer inside the ~679K slots (about 4.5M bytes): well under 0.01 h. RAM: 19 MB peak for the model on the full
stream (93,700 queued titles). Binary 620,952 B vs C3's 604,536 (+16.4 KB unpacked, PGO layout differs; not size-optimized, and its WRT decoder and dictionary loader duplicate
X2's, so they could be shared). The packed cost for S still needs measuring.

## Known limits

- Candidates are the title's coded bytes; a link whose WRT coding differs from the title line's would miss (none
  found on the slices).
- Not tested end to end in -e mode (enwik9-only pipeline); it needs E12's coded-block-header flag like X2.
