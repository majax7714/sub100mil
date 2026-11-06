# X1: cross-domain "lucky dart", physics, biology and chemistry mechanisms for byte prediction

**Verdict: NO-GO.** Survey of 25 mechanisms from physics, biology and chemistry; the two best-ranked were tested offline against the record's per-byte costs. A Potts/direct-coupling model between distinct tokens of an article (protein coevolution, trigger pairs) adds **+396 to +423 B on 10M (0.03%)** over a warm vocabulary control. Online, on a record cost log of the first 24.7M coded bytes of enwik8, it adds +277 B (0.005%), growing with history; projected **about 35 KB (10–60 KB) [ESTIMATE]** on enwik9, UPPER BOUND 150–160 KB. Coarse-grained state (renormalization / positional information / conservation) as a final-stage context: **≤ 28 B on 10M**. Every other candidate is covered by a record component or targets < 1.5% of the bits. Side finding: pre-feeding 30K other articles into a plain token n-gram model gains **3.2%** on the cold 10M slice, a measured size for the slice cold-start penalty.

Classification: **model addition (evaluation of non-transformer specialists and a final-stage context)**. Nothing was added
to the compressor. One observation-only record run (per-byte cost log on the 100M slice, no archive kept) was made, so
there is no ledger row.

Labels as in docs/design/A1-retrieval.md: **[M]** measured here, **[ESTIMATE]** derived from measurements by an argument
stated next to it, **UPPER BOUND** a ceiling, not an expected gain.

## 0. Question and method

The brief: look at how physics, biology and chemistry compress, predict or exploit redundancy, map each mechanism to
predicting the next byte of enwik9's WRT stream, check bluntly whether the record (cmix CM + frozen int4 transformer)
already covers it, and test the best one or two offline against the record's per-byte costs.

Two facts about the record frame every row below:
- The transformer was trained on all of enwik9 for 592 epochs. Anything that is a *global statistic* of enwik9
  (template field distributions, sort-key conventions, name orders) is likely already in its weights.
- Within an article it sees a 1,024-token sliding window in 3 layers plus a KDA (linear-attention) state that runs over
  the whole article. The cmix side has order-n, word, sparse, indirect, match, bracket, column (fxcm `ColumnContext`),
  PPMd order 25, SSE/APM and 23 mixers.

So the bits the record still pays are mostly **novel content** (first uses of rare names, numbers, link targets).
Mechanisms that model *structure* mostly hit regions the record already codes cheaply. The target census (§2) measures that.

Tools (all in `scripts/analysis/`):

| Step | Script |
|---|---|
| Target census: record bits on the byte regions each mechanism would act on | `x1_targets.py` |
| Per-bit mixing simulation (fork of `e11_sim.cpp`: A2's StateMap + online-mixer method, baseline = same mixer with the record only) with a Potts-coupling expert, a warm-history pre-feed, a warm n-gram control, and a coarse-state APM | `x1_sim.cpp` |

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic; X=build/exp/X1
python3 -P -E scripts/analysis/x1_targets.py --cost build/exp/A1/runs/cost_10M.bin --pre build/exp/E2/proxy/10M.pre --dict $D
g++ -O2 -march=x86-64-v3 -o $X/x1_sim scripts/analysis/x1_sim.cpp
# cold coupling (history = earlier slice articles only), alone and on top of E11's best experts
$X/x1_sim build/exp/A1/runs/cost_10M.bin build/exp/E11/aux_10M.bin lr=0.0005 nobound=1 experts=512
$X/x1_sim build/exp/A1/runs/cost_10M.bin build/exp/E11/aux_10M.bin lr=0.0005 nobound=1 experts=543
# warm: pre-feed 30K articles of full enwik9 that come AFTER the slice (no overlap), control vs control + coupling
X1_WARM=build/exp/A1/full/full.stream $X/x1_sim ... warm=30000 gucnt=1 experts=224    # warm token n-gram control
X1_WARM=build/exp/A1/full/full.stream $X/x1_sim ... warm=30000 gucnt=1 experts=736    # + coupling
# coarse-state APM: coarse=1..4 against the coarse=5 control (same APM, constant state)
$X/x1_sim ... experts=0 coarse=K
# online check on a record cost log of the 100M slice (observation-only compression, stopped at 24.7M coded bytes)
P=$PWD; R=$P/build/refs/fx2-cmix-transformer-v1; mkdir -p $X/run100M && cd $X/run100M && $R/slicetool pre $P/data/slices/enwik9_100M 100M.pre
$P/scripts/slot.sh env FX2_COSTLOG=$PWD/cost_100M.bin $P/build/exp/A1/cmix_costlog -c $R/tree/dictionary/english.dic \
  100M.pre 100M.cmix --transformer $R/tree/models/6m-q4-fp32.tfwc2; cd $P     # about 6 h contended; we stopped it at 43%
python3 -P -E scripts/analysis/x1_extract_prefix.py --cost $X/run100M/cost_100M.bin --pre $X/run100M/100M.pre --dict $D \
  --out-cost $X/p100M_cost.bin --out $X/p100M_aux.bin
$X/x1_sim $X/p100M_cost.bin $X/p100M_aux.bin lr=0.0005 nobound=1 experts=31|512|543
```
(Python ran as `python3 -P -E`, not `-I`: numpy lives in the user site-packages, which `-I` hides. No untrusted input is read.)

## 1. Candidate mechanisms (the survey)

"Covered?" is a blunt check against the record's components. Target mass is the record's cost on the region the
mechanism acts on, from §2 (10M slice) where measured.

### Biology

| # | Mechanism in its home field | Byte-level analogue (context, prediction, what feeds the mixer) | Covered? | Target mass / note |
|---|---|---|---|---|
| B1 | **Substitution-tolerant context models** (STCM) in DNA compressors: a context model that keeps following its own predicted symbols through a few mismatches (GeCo2/GeCo3, mixed by a neural net [GECO3]) | A match model that survives single-byte edits ("1987"→"1988", case flips) | **Yes.** fxcm MatchModel2 has a delta/recovery mode (cost-log flag bit 0); long-range match was A1's NO-GO | excluded (A1) |
| B2 | **Inverted repeats / reverse-complement copy experts** (GeCo IR mode; XM's reverse-complement experts [XM]) | The text analogue of a reverse complement is a *permuted* repeat: the title "Andy Warhol" reappears as the category sort key "Warhol, Andy", "An American in Paris" as "American in Paris, An". A model proposing title permutations for sort keys | **Yes, completely.** First sort keys that are title permutations cost 0.28 bits per coded byte; all sort keys on 10M cost 215 B in total [M] | ≈ 0.02% of bits, dead |
| B3 | **XM expert model**: copy experts + low-order background experts, Bayesian-averaged, with a limited expert pool [XM] | Copy experts proposing earlier offsets, mixed by recent performance | **Yes**: cmix match models + fxcm match + mixers. A1 NO-GO | excluded (A1) |
| B4 | **Protein coevolution, direct-coupling analysis (DCA)**: a Potts model with pairwise couplings J_ij between distant residues, inferred from many aligned sequences [DCA]. Same idea in language: Rosenfeld's trigger pairs [ROS] | Couplings between *distinct* tokens of an article: when a content token A first appears, raise the probability of tokens B that co-occurred with A in earlier articles (estimated P(B in article \| A in article)). Feeds per-bit StateMaps → mixer | **Partly.** The transformer's KDA state and weights carry topical associations, and the CM's n-gram contexts carry the local ones. Nothing in the record has an explicit article-level, whole-history co-occurrence table | first uses in the article: **60% of the record's bits** (754,664 B of 1,264,639 on 10M, from E11). **Tested (§3)** |
| B5 | **Profile HMMs / PSSMs** for aligned protein families [KROGH] | Per (template, field) value distribution, e.g. `\| birth_place =` | **Yes**: the field name precedes the value, so word/indirect contexts and the memorized transformer see it | infobox class 0.7% of bits |
| B6 | **Codon usage bias**: each organism prefers its own synonymous codons | Article-level "dialect": date format, British/American spelling, citation style kept consistent within an article | **Mostly**: the transformer sees earlier choices within its window and KDA; E10/E11 adapt to the article | unlinked dates are 0.02% of bits [M]. Low |
| B7 | **Clonal selection / affinity maturation**: a population of detectors, the best cloned and hypermutated [CLONALG] | An online evolving pool of sparse context masks: spawn mutants of the best-performing contexts, retire the worst | Conceptually not (cmix contexts are fixed, tuned offline), but there are hundreds of contexts already and each new one is worth little | expensive to test well (needs the real mixer); expected small |
| B8 | **Positional information / morphogen gradients**: cells read position from a concentration gradient [WOLP] | Offset inside the article and the line-start kind as a context | Not explicitly, but the record's bpb is flat from 1K to 64K coded bytes into an article [M] | **Tested (§4)**: 0 B |
| B9 | **Semi-Markov gene finders** (explicit segment-length distributions, GENSCAN) | A hazard model for where a paragraph, line or table row ends | Partly (word/line contexts, transformer) | paragraph-ending newlines are 0.19% of bits [M]; ceiling ~0.05% |
| B10 | **Burstiness of recurrences**: stretched-exponential (Weibull) recurrence times of words [ALT] | A recency kernel with power-law/Weibull rather than exponential decay for token recurrence | E11 tested exponential recency (τ = 30–1,000) and whole-file caches. Overlaps E11/E10 | excluded (E11) |

### Physics

| # | Mechanism in its home field | Byte-level analogue | Covered? | Target mass / note |
|---|---|---|---|---|
| P1 | **Renormalization group** / multiscale coarse-graining (Kadanoff blocks; the RG↔deep-learning mapping [RG]) | A coarse state from a larger scale (the current section heading, offset bucket, line kind) as an APM/mixer context | **Mostly**: the transformer is a learned coarse-grainer; mixer selectors see local structure | headings are 0.79% of bits [M]. **Tested (§4)**: ≤ 28 B on 10M |
| P2 | **Criticality / power-law mutual information** (text's MI decays as a power law; Markov models cannot reproduce it [LT]) | Diagnostic: is there long-range dependence the record misses? | Record bpb does not rise past the transformer's 1,024-token window (1.92 bits/coded byte at 1–2K and 2–4K, 1.83 at 4–8K) [M] | no deficit visible |
| P3 | **Ising / maximum-entropy models** (Jaynes) | Pairwise feature model of the next bit | **Yes**: logistic mixing *is* a max-ent model over the inputs | — |
| P4 | **Kalman filter / state estimation** | (a) Track numeric sequences (years, counts) as a hidden linear state; (b) second-order, Kalman-gain mixer weight updates | (a) E12's territory (excluded). (b) The record's internal mixers can't be re-simulated from the final pbits; at the final stage only recalibration is left (≈ 50 B on 10M) | low |
| P5 | **Conservation laws** (Noether; charge balance) | Open/close balance of `[ ]`, `{ }`, `''`: predict closers from the nesting depth | **Yes**: cmix `bracket` model; closing `]` costs 0.51 bits [M] | 0.53% of bits (mostly *where* a link ends, not nesting). **Tested (§4)**: 0 B |
| P6 | **Spectroscopy / Fourier**, crystallography's **Patterson autocorrelation** to find periods | Detect row periods in tables and predict a cell from the cell above | **Yes**: fxcm `ColumnContext` (last 3 rows + current) | table separators 0.01% of bits [M]; table class 1.7% |
| P7 | **Temperature / annealing** (Boltzmann) | Adapt the transformer's softmax temperature online | **Yes**: E2's logit head and the final SSE | — |
| P8 | **Hopfield associative memory** | Content-addressable recall of earlier text | Long-range retrieval, excluded (A1) | excluded |

### Chemistry

| # | Mechanism in its home field | Byte-level analogue | Covered? | Target mass / note |
|---|---|---|---|---|
| C1 | **Reaction kinetics** with fast and slow pools (multi-timescale rate equations) | Dual-rate counters: a fast and a slow estimate per context, mixed | **Yes**: CM state machines + adaptive StateMaps, run/rate variants | — |
| C2 | **Chemical equilibrium / detailed balance** | Balance of opening vs closing tokens | Same as P5: covered | — |
| C3 | **Kinetic proofreading**: two sequential discrimination steps square the error rate [KP] | Trust a candidate byte only when two independent predictors agree (two-stage verification) | **Yes**: SSE/mixer selection; E11 tried "cache agrees with record" gating, which made it worse | — |
| C4 | **Hammett linear free-energy relationships**: substituent effects add in log-rate space [HAM] | Context effects that add in logit space | **Yes**: that is logistic mixing | — |
| C5 | **Valence rules / formal grammars of molecules** (SMILES) | Template "valence": a parameter already used in this template instance won't recur; required parameters still missing are likely | Within the transformer's window | templates 1.2% of bits; low |
| C6 | **Mendeleev's gap prediction** from the periodic table's regularity | Completing systematic series (years, ordinals, "1990 in film") | E12's territory (numeric consistency), excluded | excluded |
| C7 | **Crystal symmetry** (translation, mirror) | Row translation in tables (P6) and mirror symmetry of open/close tags (P5) | Covered (column + bracket models) | merged into P5/P6 |

### Ranking

Score = plausible gain on the bits the record still pays × cheapness to test offline:

1. **B4 Potts / DCA couplings between distinct tokens of an article.** It is the only candidate aimed at the 60% of
   bits on first uses, where E11's self-cache *loses*. Testable with the E11 harness. **Tested (§3).**
2. **P1 + B8 + P5 coarse-grained state (section, position, nesting) as a final-stage context.** Cheap; the target
   census already suggests little. **Tested (§4).**
3. B7 evolving context pool: not covered conceptually, but needs the real mixers to evaluate; small expected gain.
4. B9 segment-length hazard: ceiling about 0.05% of bits.
5. B6 article dialect: within the transformer's reach; small region.

Everything else is covered by a record component or excluded by the brief (A1, A2, E11, E12, A3).

## 2. Target census on 10M [M]

`x1_targets.py`, record cost on the 10M slice (1,264,639 B):

| region (mechanism) | spans | coded B | record B | bits / coded B | % of bits |
|---|---:|---:|---:|---:|---:|
| category sort key, first in article, = title permutation (B2) | 98 | 1,129 | 39 | 0.28 | 0.003% |
| all category sort keys (B2) | 1,148 | 11,179 | 215 | 0.15 | 0.02% |
| closing `]` (P5) | 106,212 | 106,212 | 6,725 | 0.51 | 0.53% |
| closing `}}` (P5) | 611 | 1,222 | 129 | 0.84 | 0.01% |
| closing `''` / `'''` (P5) | 20,824 | 46,494 | 2,070 | 0.36 | 0.16% |
| section heading text (P1) | 5,534 | 40,781 | 9,937 | 1.95 | 0.79% |
| table row/cell separators (P6) | 7,780 | 9,919 | 188 | 0.15 | 0.01% |
| paragraph-ending newline (B9) | 14,888 | 14,888 | 2,402 | 1.29 | 0.19% |
| unlinked month-day-year dates (B6) | 176 | 2,043 | 313 | 1.23 | 0.02% |
| all digits (E12, reference) | 67,567 | 191,300 | 49,596 | 2.07 | 3.92% |

By offset from the page start (coded bytes; the transformer window is 1,024 tokens): 1.06 bits/coded byte for the first
256 bytes, then 1.84, 1.92, 1.92, 1.83, 1.91, 1.83, 1.79 for 256–1K, 1–2K, 2–4K, 4–8K, 8–16K, 16–32K, 32K+.
There's no rise past the window, so no visible long-range deficit (P2).

**Reading:** the structural regions that physics- and chemistry-style mechanisms act on (nesting, periodicity, symmetry,
sort-key permutations) together hold under 1.5% of the record's bits, and the record pays well under a bit per coded byte
on most of them. The DNA inverted-repeat analogue looked the most original. It's empty: the memorized transformer
already writes "Warhol, Andy" at 0.28 bits per byte.

## 3. Test 1: Potts / direct-coupling model between distinct tokens (B4)

**Model** (`x1_sim.cpp` experts 8 and 9; tokens as in E11, in the coded stream):
- A content token is a token of ≥ 2 coded bytes. That excludes the 1-byte dictionary codes of the commonest words.
- Postings: for each content token A, the last `pc` = 16 articles that contained it. Each finished article also keeps
  its first `ta` = 200 distinct content tokens.
- When a content token A appears for the first time in the current article and is a *source* (seen in 1 to `dmax` = 64
  earlier articles, at most `ks` = 100 sources per article), every token B of A's posted articles gets mass
  `cw`/|post(A)|. B's mass is then Σ_A P̂(B in article | A in article), the empirical coupling.
- The B's go into a per-article prefix trie, and the per-bit StateMaps and mixer work exactly as in E11.
  Expert 9 (CPLN) skips any B already used in this article, so it is pure cross-coupling, complementary to E11's
  self-cache. Expert 8 (CPL) keeps them.
- This is a pairwise-only coupling estimate (mean-field DCA would also remove indirect couplings), with no
  learning beyond counting.

**Three history regimes**, because the slice is cold and the full run is not:
- **Cold:** couplings learned online from the earlier articles of the slice only (1,325 articles on 10M).
- **Warm:** before the slice, pre-feed `warm` articles of full enwik9 that come *after* the slice (offset 5,353,615 of
  the identity-order WRT stream, so there's no overlap with the slice), then continue online. This is a proxy for the
  middle of a full run.
- **Warm control:** the same pre-feed into a *non-coupled* vocabulary model. It is a whole-history unigram over article
  counts (`gucnt=1`, expert 5), plus token bigram and trigram continuations (experts 6 and 7). It stands in for what the
  warm CM of a full run already knows about those articles' tokens. Coupling is credited only with what it adds over
  this control.

**Results [M]** (gain = baseline − with experts; baseline = the same mixer on the record alone):

| slice | configuration | gain | coupling's own share |
|---|---|---:|---:|
| 1M | cold CPL / CPLN | −0 / −0 B | 0 |
| 1M | cold E11 best / E11 + CPLN | +30 / +23 B | **−7 B** |
| 1M | warm 30K: n-gram control / control + CPLN | +3,783 / +3,812 B | **+29 B (0.023%)** |
| 10M | cold CPL / CPLN | −10 / −12 B | **−12 B** |
| 10M | cold E11 best / E11 + CPLN | +702 / +658 B | **−44 B** |
| 10M | warm 120K: CPLN alone | +5,595 B | (confounded, see below) |
| 10M | warm 120K: unigram control / control + CPLN | +13,731 / +15,600 B | +1,869 B (0.148%) |
| 10M | warm 120K: E11 + unigram control / + CPLN | +15,185 / +17,284 B | +2,099 B (0.166%) |
| 10M | warm 30K: n-gram control / control + CPLN | +40,510 / +40,906 B | **+396 B (0.031%)** |
| 10M | … same, `cw=4` / `dmax=16 ks=200` | +40,933 / +40,931 B | +423 / +421 B |

**What this says:**
1. **Cold, the coupling model is useless.** With 1,325 articles of history the couplings are too sparse, and
   they slightly hurt E11.
2. **Warm, most of the apparent gain is plain vocabulary, not coupling.** CPLN alone gains 5.6 KB (0.44%) with
   120K pre-fed articles. Almost all of it is on tokens *new to the slice* (+5.3 KB) and on the 3rd+ coded byte of a
   token, i.e. completing a rare word the cold slice CM has never seen. A plain warm unigram over the same articles
   gains 13.7 KB, and a warm n-gram control 40.5 KB (3.2% of the record on a 10M slice!). That is the cold-start
   handicap of slice measurements. A full run's CM doesn't have it, because it has already seen those tokens.
3. **Over the n-gram control the coupling adds about +0.03%** (+396 to +423 B on 10M, +29 B on 1M), robustly across
   the settings tried. Over the weaker unigram control it adds 0.15%, so token-bigram context already explains about
   80% of what the couplings know. The gain sits on first uses in the article (+385 B) and mostly on link targets
   (+203 B), then prose (+76 B). By half of the slice: +212 B, then +183 B. With a fixed 30K pre-fed history it doesn't grow within the slice. The
online runs below do grow, because there the history itself grows.

**Online check with a warmer CM [M].** The pre-feed controls are proxies. A real implementation would learn the couplings
online, and the record's CM would learn from the same history. To see both together, the record was run with
the per-byte cost log on the 100M slice (enwik8; `build/exp/A1/cmix_costlog`, compression only, observation only,
`build/exp/X1/run100M/`). The coupling was simulated online on prefixes of that log as it was written
(`x1_extract_prefix.py` cuts at the last complete page, and the decode is checked byte-for-byte against the slice's `.pre`):

| prefix | pages of history | E11 best | CPLN alone | E11 + CPLN | coupling's own share |
|---|---:|---:|---:|---:|---:|
| 12.6M coded bytes (21.4 MB text) | 3,170 | +1,832 B (0.065%) | −16 B | +1,863 B | **+31 B (0.001%)** |
| 24.7M coded bytes (42.1 MB text) | 5,817 | +3,401 B (0.062%) | +53 B | +3,678 B | **+277 B (0.005%)** |

The run was stopped at 24.7M of about 58M coded bytes (43%), because the trend was clear and the run held a slot for
about 6 h. The baselines differ by up to 96 B between configurations, since the coupling's has-opinion flag also
selects mixer weight sets. Final sizes, E11 + CPLN against E11 alone: −373 B at 24.7M.

The coupling's share on top of E11 is negative in the first decile and grows with history:
- 12.6M prefix: about +14 B per decile of 1.26M coded bytes late on (deciles 6–9), about 0.005% of those bytes' cost.
- 24.7M prefix: +52, +35, +66, +58 B per decile of 2.47M coded bytes in deciles 6–9, about 0.010%.

So the online rate roughly doubles as the history doubles (3.2K → 5.8K pages). Coupling statistics fill in with
history, which is why the cold slices show nothing.

### Projection to full enwik9 [ESTIMATE]

- **Central, about 35 KB (range 10–60 KB).** Two independent routes agree:
  - **Warm proxy:** over the warm n-gram control (30K pre-fed articles), the coupling adds 0.031–0.033% of the record
    on 10M, robust to `cw`, `dmax`, `ks`. Applied to the 97.0 MB full archive, that is 30–32 KB.
  - **Online trend:** about 0.005% late at 3.2K pages and 0.010% at 5.8K pages of history. If each doubling of
    history adds about +0.005% (log growth), the full run's 243K pages are 5.4 more doublings, giving about 0.037%,
    or about 36 KB. Growth linear in history would be far higher. It is ruled out by the 120K-article warm proxy,
    which is capped at 0.15% even against a unigram-only control.
  - That control is far weaker than the full run's CM. It has token unigrams, bigrams and trigrams only: no byte
    order-n contexts, no PPMd order 25, no match models, no word/indirect models. So the true increment over a warm
    record is lower.
  - The online route uses the record's own CM as the control, but only a 6K-page history to extrapolate from. The
    range covers either route being off by about 2× in either direction.
- **Overlap.** E10 (dynamic evaluation) and E2 adapt the transformer to the article's topic, the same signal the
  couplings carry. The share left on top of C1/E10 would be smaller still.
- **UPPER BOUND, about 150–160 KB.** Over a *unigram-only* control the coupling adds 0.148–0.166%. That bound
  applies only if the full run's CM knew nothing about token sequences beyond unigram frequencies. It knows far more:
  the n-gram control alone already removes about 80% of the coupling's apparent gain.

### Cost of a real implementation [ESTIMATE]

- **RAM:** at full scale, postings of 16 article ids for every content token (several million distinct content tokens)
  plus 200 token ids per article (243K articles) come to about 0.6–1.2 GB. That is 2–4× the record's whole RAM headroom
  of about 0.3 GB, so it would need memory taken from PPMd or fxcm.
- **Time:** about 320K trie insertions per article (100 sources × 16 articles × 200 tokens), about 130 per coded byte.
  At ~50 ns each that is about 6–7 µs per coded byte, or about 1.4 h on the reference machine, within the ~48 µs
  headroom but not free.
- **S:** a few KB of code.

## 4. Test 2: coarse-grained state as a final-stage context (P1 renormalization, B8 positional information, P5 conservation)

An interpolated APM (33 buckets × 8 bit positions × coarse state, rate 0.02) on stretch(p_record) feeds one extra
input to **both** the baseline and the expert mixer. `coarse=5` is the control: the same APM with a constant state, so
plain recalibration isn't credited. The states are tracked causally from the coded bytes (logical characters through
the WRT punctuation swap).

| coarse state | 1M baseline | 10M baseline | vs control (10M) |
|---|---:|---:|---:|
| 5: control (APM, constant state) | 126,418 | 1,264,702 | — |
| 1: offset-in-article bucket (8, log2 from 256) × line-start class (12) | 126,417 | 1,264,703 | +1 B (worse) |
| 2: hash of the current section heading (256) | 126,417 | 1,264,701 | −1 B |
| 3: heading hash × offset × line class (4,096) | 126,415 | 1,264,674 | **−28 B** |
| 4: `[ ]` depth × `{ }` depth × italic/bold state × heading line | 126,417 | 1,264,702 | 0 B |

**≤ 28 B on 10M (0.002%), about 3 KB at full scale [ESTIMATE]: dead.** The record's mixer selectors and the transformer
already carry section, position and nesting state. This matches the target census (§2): these regions are cheap
and the bpb profile over the article is flat.

## 5. Verdict and what it points to

**NO-GO.** No cross-domain mechanism clears the 100 KB bar.
- The tested candidate is the Potts/DCA couplings between distinct tokens, the one that aims at the 60% of bits on
  first uses. It projects to about **35 KB (10–60 KB) [ESTIMATE]**, with an unreachable 150–160 KB UPPER BOUND.
- Coarse-grained state (renormalization, positional information, conservation) is worth ≤ 28 B on 10M.
- The rest of the survey is either covered by a record component or targets regions that hold < 1.5% of the bits.

**Where the gains of physics, biology and chemistry already live in the record:**
- logistic mixing (max-ent, Hammett additivity);
- SSE/APM (kinetic proofreading, gain control);
- the column and bracket models (crystal periodicity, conservation);
- fxcm's recovering match model (substitution-tolerant DNA models);
- the memorized transformer (profiles, inverted-repeat sort keys).

The sciences converged on these same tools.

**Findings worth keeping:**
1. **The slice cold-start penalty is large and measurable.** Pre-feeding a plain token n-gram model with 30K *other*
   enwik9 articles gains 3.2% of the record on the cold 10M slice (40.5 KB) and 3.0% on 1M. CM-side mechanisms that
   "learn vocabulary" will look 10–100× better on a slice than at full scale. Any future CM-side specialist needs a
   warm control like `gucnt=1 experts=224` before it is believed. This backs the EXPERIMENTS.md slice caveat with a number.
2. **The DNA inverted-repeat analogue is real in the text, but memorized.** Category sort keys are title permutations
   ("Warhol, Andy"), and the record codes them at 0.15–0.28 bits per coded byte.
3. The coupling's residual sits on **first uses of link targets and rare names**, the same bits A2 and E11 couldn't
   reach. Topic adaptation of the transformer (E10, E2) is the better-placed tool for that signal than a parallel
   byte predictor.

**What we'd try differently:** a coupling signal fed *into the transformer side* (e.g. as a bias on E2's logit head from
the article's rare-token set) rather than as a byte-level expert. Re-measure it on top of a C1/E10 cost dump with a warm control.
Of the untested candidates, only B7 (an evolving context pool) is not covered in principle, and it needs the
real mixer to evaluate.

## Sources

- [GECO3] Silva, Pratas, Pinho, *Efficient DNA sequence compression with neural networks*, GigaScience 9(11), 2020.
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC7657843/>
- [XM] Cao, Dix, Allison, Mears, *A simple statistical algorithm for biological sequence compression*, DCC 2007.
  <https://allisons.org/ll/Publications/2007DCC/>
- [DCA] Morcos et al., *Direct-coupling analysis of residue coevolution captures native contacts across many protein
  families*, PNAS 108(49), 2011. <https://pmc.ncbi.nlm.nih.gov/articles/PMC3241805>
- [ROS] Rosenfeld, *A maximum entropy approach to adaptive statistical language modelling*, Computer Speech and
  Language 10, 1996. <https://www.eecs.yorku.ca/course_archive/2007-08/W/6328/Reading/Rosenfeld_MELM.pdf>
- [LT] Lin, Tegmark, *Critical behavior in physics and probabilistic formal languages*, Entropy 19, 299, 2017.
  <https://arxiv.org/abs/1606.06737>
- [ALT] Altmann, Pierrehumbert, Motter, *Beyond word frequency: bursts, lulls, and scaling in the temporal
  distributions of words*, PLoS ONE 4(11) e7678, 2009. <https://pmc.ncbi.nlm.nih.gov/articles/PMC2770836/>
- [RG] Mehta, Schwab, *An exact mapping between the variational renormalization group and deep learning*, 2014.
  <https://arxiv.org/abs/1410.3831>
- [CLONALG] de Castro, Von Zuben, *Learning and optimization using the clonal selection principle*, IEEE TEVC 6(3),
  2002. <https://doi.org/10.1109/TEVC.2002.1011539>
- Classics cited from memory (not re-fetched): [KP] Hopfield, *Kinetic proofreading*, PNAS 71(10), 1974;
  [WOLP] Wolpert, *Positional information and the spatial pattern of cellular differentiation*, J. Theor. Biol. 25, 1969;
  [KROGH] Krogh et al., *Hidden Markov models in computational biology*, J. Mol. Biol. 235, 1994;
  [HAM] Hammett, *The effect of structure upon the reactions of organic compounds*, JACS 59, 1937; Kalman, J. Basic
  Eng. 82, 1960.
