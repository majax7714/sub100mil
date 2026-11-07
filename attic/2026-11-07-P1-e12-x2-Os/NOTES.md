# P1 sub-item: E12/X2 namespaces at -Os (no-go)

**Idea.** E12 (arith specialist) and X2 (time law) are mostly cold, per-slot code compiled at -O3 with PGO in the
predictor.cpp TU. Wrap both namespaces in `#pragma GCC push_options` / `optimize("Os")` to shrink them.

**Code.** `patches/exp/P1-packaging/0003-P1-e12-x2-Os.patch`, applied by `build_p1.sh` only with `P1_OS=1`
(commit 8be82f8). Build: `P1_OS=1 P1_OUT=c3p1os patches/exp/P1-packaging/build_p1.sh`.

**Result (2026-11-07).** On top of the P1 build (C3 + regex-free E12 + bit_head -O2 + no unwind tables):
.text 428,696 -> 427,224 B (-1,472 raw); the file size is unchanged (501,024 B, page alignment) and the p1cm-packed
exe gets *larger*: 185,818 -> 185,858 B (+40). Not benched for bit-exactness, since there's nothing to gain.

**Why it failed.** Under `-flto` + PGO most of E12/X2's size is in std container/string template instances
(defined outside the pragma region, so they keep -O3) and in code that LTO had already sized; the attribute
reaches little. -Os code also packs worse per byte than -O3 code.

**What we'd try differently.** Removing the code instead of re-optimizing it: replace std::map/unordered_map/
std::string-heavy candidate maps with flat arrays, or build E12's tables at startup from a compact description.
Or move cold code into its own non-PGO TU compiled with -Os (needs the FP math kept bit-identical, so verify).
