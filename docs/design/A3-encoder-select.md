# A3: encoder-side selection with decoder replay, design and evidence

**Verdict: NO-GO.** The encoder forks a cheap component's state at the start of each block, tries K variants, commits
the best one and sends the choice. The decoder replays only that variant. The first target was the E2 online logit
head, with K learning-rate multipliers. On the 10M slice the **upper bound**, a per-block oracle over K = 10
independent head trajectories, is worth only **−149 B of head loss net of side info vs the best fixed LR (16K-token
blocks), about −83 B coded [ESTIMATE]**. The decision rule asked for −380 B. The **realizable** version
(fork-and-commit) is worse still. Greedy per-block selection *loses* to the best fixed LR at every block size of
16K tokens or less: what wins inside one block (often a larger step, sometimes a frozen head) leaves a worse state for
the blocks after it. With an H-block lookahead the best point is −42 B of head loss net (≈ −24 B coded), at
30× the head cost in the encoder (§3). No coder change was built. The one
useful by-product is plain tuning: halving E2's LR (a 0.25→0.05) is the best fixed setting, at −427 B of head loss on
10M (§4, bench row).

Labels as in docs/design/A1-retrieval.md:
- **[M]** measured here.
- **[ESTIMATE]** derived from measurements by an argument stated next to it.
- **UPPER BOUND** is a ceiling, not an expected gain.

Classification: **architectural** (a new way of spending the compute budget, evaluated offline).

## 0. Method and reproduction

The E2 logit head reads only the transformer's normalized final hidden state `xn` (192 floats) and the static head's
raw logits (205). Both are fixed by the frozen transformer and do not depend on the head. So an encoder can buffer a
block of them and replay the head K times. That is also why the head can be simulated exactly offline:

| Step | What | Where |
|---|---|---|
| Dump | Transformer-only run of the E2 build (`E2_LOGIT=1`, default rates) writing `y, xn, raw_logits` for every token the head predicts. 1M: 552,115 tokens (97 s); 10M: 5,763,179 tokens (945 s, 9.2 GB, RSS 1.25 GB) | `patches/exp/A3-encoder-select/{0001-a3-dump.patch,dump.sh}` |
| Head replay | The E2 logit head with the same op order and glue.cpp's softcap/softmax, any LR multiplier m: `indep`, `fork`, `forkla`, `fixed` modes | `scripts/analysis/a3_sim.cpp` |
| Oracle, side info, mixture | Per-block oracle over `indep` losses, naive (log2 K) and adaptive (order-1 KT) choice cost, fixed-share mixture of the K heads | `scripts/analysis/a3_oracle.py` |

Commands are in `patches/exp/A3-encoder-select/README.md`. Variant set: m ∈ {0, 0.25, 0.354, 0.5, 0.707, 1, 1.414, 2,
2.828, 4} (K = 10), applied to E2's whole schedule `a_t = 0.1 + 0.4/(1 + t/8000)`. m = 0 freezes the head for the
block.

**Validation [M].** The replay matches the in-codec head: on 1M, sim m=1 minus sim m=0 = −1,429 nats, against −1,421
nats for E2 minus static in the real run. The small rest comes from the 173 piece-start tokens and the fp16 row.

**Calibration from head loss to coded bytes [M].** On 10M, E2's head improves on the static head by 20,971 nats
(3,782 B), and E2 is worth −2,123 B coded. So 1 B of head loss ≈ 0.56 B coded (0.83 on 1M). Applying the same factor
to *marginal* head improvements is an assumption [ESTIMATE].

## 1. Upper bound: oracle over independent trajectories [M]

10M, 5,763,179 head tokens. Head loss in bytes (nats/ln2/8). Negative is a gain.

| Fixed multiplier m | 0 | 0.25 | 0.354 | **0.5** | 0.707 | 1 (E2) | 1.414 | 2 | 4 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| head loss vs m = 1, B | +3,785 | −156 | −343 | **−427** | −342 | 0 | +714 | +1,947 | +6,697 |

The best fixed LR is m = 0.5, and the curve is flat between 0.35 and 0.7. Per-block oracle, choosing the best column
in each block:

| Block (tokens) | Blocks | Oracle vs m=1 | Oracle vs best fixed | Side info (adaptive) | **Net vs best fixed** | Coded ×0.56 [ESTIMATE] |
|---:|---:|---:|---:|---:|---:|---:|
| 4,096 | 1,408 | −960 | −533 | +491 | −42 | −24 |
| 16,384 | 352 | −686 | −260 | +111 | **−149** | **−83** |
| 65,536 | 88 | −577 | −151 | +24 | −127 | −71 |
| 262,144 | 22 | −518 | −91 | +6 | −85 | −48 |
| 1,048,576 | 6 | −477 | −50 | +2 | −48 | −27 |

- The choices cluster around the global optimum: at 16K, 0.354–0.707 win 269 of 352 blocks. The best LR barely moves
  between blocks, so there is little to select.
- 1M behaves the same way: the best fixed m is 0.5 (−44 B), and the oracle nets −23 B at 16K.
- This bound is *optimistic*. Taking the per-block minimum over 10 noisy trajectories also picks up noise, and
  independent trajectories never feel the cost of a switch.

## 2. Symmetric alternative: run all K heads in the decoder too [M]

A fixed-share mixture of the 10 heads' token distributions needs no side info. Its cost is K× the head in *both*
directions. It gains −323 B (α = 1e-4) to −362 B (α = 1e-3) of head loss vs the best fixed head. That is 2.2–2.4×
the encoder-select upper bound, at K = 10 (≈10 h scaled, so unaffordable). Soft per-token mixing beats hard per-block
selection here. That fits the general picture: the CM's mixers already do per-bit soft selection for free.

## 3. Realizable: fork-and-commit [M]

Shared state. At each block, every variant runs from the committed state, and the winner's state is committed. The
table gives total head loss vs the best fixed m = 0.5, before side info.

| Block | Greedy (score = this block) | Lookahead H blocks with m = 0.5 after (score = block + next H) |
|---:|---:|---:|
| 4,096 | **+1,650 B** (worse) | H=4: **+268 B** (worse) |
| 16,384 | **+410 B** (worse) | H=4: −60 B; side info +101 B → net **+42 B**. K=3 {0.354, 0.5, 0.707}, H=4: −0.08 B; side info +58 B |
| 65,536 | −6.5 B (−36 nats; side info +24 B, so net +18) | H=2: −64 B; side info +22 B → net **−42 B** (≈ −24 B coded), the best realizable point |
| 262,144 | +16 B | |

The best realizable point (64K blocks, K = 10, H = 2) runs the head 30 times per token in the encoder, about 30 h
scaled. That is impossible, and it is still worth only about −24 B coded. With a lookahead, the picks shift to the
low side (m = 0.25 or 0.354 in 72 of 88 blocks), because the default 0.5 continues after the chosen block.

On 1M, greedy is worse than the best fixed by +326 B (1K blocks), +110 B (4K), +13 B (16K) and +6 B (64K). Lookahead
H=4 gives −0.5 B (4K) and −4 B (16K), both before 13–54 B of side info.

**Why greedy fails:** the block-local score is myopic. Starting from the greedy trajectory's own states, m = 1
beats m = 0.5 *inside* the block (summed over the 4K-token blocks: 7,060,634 vs 7,061,289 nats). A larger step fits
the current block's local statistics, but it leaves a noisier state for the blocks after it. At 4K blocks greedy
spreads its picks over the extremes: m = 0 (freeze) 192 times, m ≥ 1.414 516 times, and the global optimum 0.5 only
103 of 1,408 times. A lookahead removes the bias, but it multiplies the encoder cost by (1+H). Even then it does not
reach the oracle, because the oracle's gain is mostly noise selection that does not carry over to the committed
state.

## 4. By-product: retune E2's LR (tuning) [M]/[ESTIMATE]

m = 0.5 (`E2_A0=0.25 E2_AF=0.05`) is the best fixed LR on both 1M and 10M: −427 B of head loss on 10M → about −240 B
coded [ESTIMATE]. Bench on top of C1 (`E2_A0=0.25 E2_AF=0.05`, method `fx2T+C1-a3-lr05`): pending.

## 5. Cost (ESTIMATE)

E2 head ≈ 5 µs/token on the dev box (E2 README, `perf stat`). Full enwik9 has 586.46M tokens, and dev-box hours are
scaled ×1.26 to the record's 3700X timing.

| Setting | Encoder extra | Decoder extra |
|---|---|---|
| K = 4, greedy | 3 × 5 µs → ≈ 3.1 h of the ≈ 7.8 h headroom | ≈ 0 (the choice decode, one 160 KB state copy per block for the encoder only) |
| K = 10, greedy | 9 × 5 µs → ≈ 9.2 h (**over** the headroom) | ≈ 0 |
| K = 10, lookahead H = 4 | 49 × 5 µs → ≈ 50 h (impossible) | ≈ 0 |

The decoder does pay ~1×, as intended. But the encoder has the same ≈45 h limit and is already at 37.2 h, so the
asymmetry only buys a K of `1 + 7.8 h / (component cost)`. That restricts it to components costing well under 1 h
at full scale.

## 6. Where else the mechanism could apply (rough bounds, ESTIMATE)

Rule of thumb from this measurement: selecting a hyperparameter of an *adaptive* learner per block is worth at most
about 4% of that learner's own contribution (oracle −149 B of the head's 3,782 B over static, 0.56 coded), before the
myopia loss. Learners self-correct, so their optimal hyperparameters are flat and stable.

| Target | Encoder cost of K = 4 (scaled) | Fork state | Bound on 10M | Verdict |
|---|---|---|---|---|
| E2 head LR (this) | ≈ 3.1 h | 160 KB | ≲ −85 B | measured NO-GO |
| E3 BITLSTM LR | ≈ 4.2 h | ~1.4 MB | ≲ 4% of −324 ≈ −13 B | no |
| E10 dyneval LR/decay | ≥ 3× the transformer backward plus a forward replay of the transformer (state = KV/KDA): ≫ headroom | transformer state | ≲ 4% of −2,697 ≈ −110 B | unaffordable |
| Final SSE/APM rates (0.8% of time ≈ 0.3 h) | ≈ 0.9 h | small tables | ≲ a few % of the SSE stage's own gain; likely < 100 B | not worth a build |
| Final mixer (layer 1) LR | < 0.5 h | small | same rule; < 100 B | not worth a build |
| 23 layer-0 mixer LRs | ≈ 3 × 15.6% of 37 h ≈ 17 h | GB-scale weights | — | unaffordable |
| Whole CM sub-model set per article | K × most of the run | GBs | — | unaffordable |

Where hard selection *could* beat soft mixing is a **discrete, non-learnable state decision** that a mixer cannot
express and that changes what later blocks see. Examples: whether to carry the transformer context across an article
boundary instead of resetting it, per-article preprocessing on/off, or a per-article dictionary or WRT variant. These
are not "cheap components". Their encoder cost is a re-run of the transformer (≈52% of time) or a two-pass encode, so
they do not fit the 7.8 h either, unless the decision can be made from a cheap proxy computed once. And then it is no
longer encoder *search*, only encoder-side analysis sent as side info.

## 7. What we'd try differently

- Measure the hyperparameter's **non-stationarity** before building fork machinery: here a single `indep` run
  (≈5 min on 10M) settled it. If the per-block winners cluster at the global optimum, there is nothing to select.
- Prefer a **soft, symmetric** alternative when the decoder can afford K copies (§2 is 2.4× better), or simply retune.
- The asymmetric idea only pays where the encoder has slack the decoder does not. In this prize both programs have the
  same limit and the record uses about 83% of it in both directions, so the asymmetry buys little.
