# Compute: free and cheap GPU budget for training the byte model

Survey date: **2026-11-04**. This is read-only web research. Every number has a source tag (URLs at the end, all
retrieved 2026-11-04). **UNVERIFIED** marks anything I could not confirm from an official page. **ESTIMATE** marks
my own arithmetic.

All "RTX-5090-equivalent hours" (5090-eq h) below are **FLOP-ratio estimates** from dense bf16/fp16 tensor-core
spec-sheet peaks. Real throughput will differ, often a lot (see §5.4).

---

## 0. Monthly budget summary

**Your two assumptions, checked:**

- **Kaggle: 30 hours per *week*, not per month.** The docs say "The quota resets weekly and is 30 hours or sometimes
  higher depending on demand and resources" [KGPU]. That is about **130 GPU-session-hours per month**
  (30 × 52 / 12 = 130). It's about 4.3× what you assumed.
- **Modal: $30 per month, recurring.** The Starter plan is "$0 + compute / month" with "$30 / month free credits".
  The FAQ says Starter "includes $30 of compute (GPUs, CPUs, Memory) per month" [MODALP]. GPUs require a payment
  method on file [MODALGPU]. The page does not say whether unused credit rolls over (assume it does not).

**Recurring total, in 5090-eq hours per month (ESTIMATE):**

| Source | Ceiling (ideal FLOP ratio) | Realistic planning figure |
|---|---|---|
| Kaggle GPU, 130 session-h/mo | T4×2 with perfect 2-GPU scaling: 130 × 0.621 = **80.7** | Only one T4 effectively used (a 6–8M model on 2 GPUs scales poorly; queues, idle time): 130 × 0.310 = **40.3** |
| Modal $30/mo | All on B200, net of a 2-core/16-GiB host: 4.64 h × 10.74 = **49.8** | All on H100 (a d=192 model is unlikely to fill a B200): 7.19 h × 4.72 = **34.0** |
| **Total** | **≈ 130 5090-eq h/mo** | **≈ 74 5090-eq h/mo** |

Not counted in the totals:

- **Kaggle TPU v3-8, 20 h/week (≈ 87 h/mo).** On FLOPs this is ≈ 204 5090-eq h/mo [KTPU][TPUV3]. In practice the
  fx2-T/neif models use Kimi-Linear (KDA) CUDA/Triton kernels, which won't run on TPU without a JAX or PyTorch/XLA
  port. It's the biggest free pool if someone ports the model.
- **Colab free.** Limits aren't published [COLABFAQ].
- **HF ZeroGPU.** 5 min/day, inference-shaped Gradio calls only [HFZG].
- **Camber.** 5 GPU h/mo through the GitHub Student Pack; GPU type is UNVERIFIED [GHSP].

**Plan around ~75 5090-eq h/month.** At that rate, reproducing fx2-T's 208 GPU-h run takes **≈ 2.8 months**
(1.6 months at the ceiling). The ~2,800 GPU-h neif effort takes **≈ 38 months** (21.5 months at the ceiling). See §5.

**One-time credits worth claiming now:**

1. **Modal academic credits: "up to $10k"** for "Graduate students, labs, and researchers" [MODALP]. An undergrad
   probably needs a faculty sponsor or lab. Eligibility details are UNVERIFIED. At B200 rates, $10k is about
   1,600 h, roughly 17,000 5090-eq h on FLOPs. That's the single biggest lever on this list.
2. **Google TPU Research Cloud (TRC).** It's still listed with an "Express interest" form and no pause notice
   [TRC][TRCFAQ]. TPUs are free, but you pay for the small VMs and GCS storage. It only helps if the model is ported
   to TPU.
3. **Lightning AI.** 5 credits at sign-up plus 25 more when you add a card, "Up to 30 credits". That's "about 50 GPU
   hours" on interruptible machines. Credits expire after 12 months [LIGHT].
4. **Colab Pro for Education.** This was a free year of Colab Pro for US higher-ed students (announced 18 Jul 2025,
   "a limited number") [COLABEDU]. A secondary source says it's closed to new sign-ups (UNVERIFIED). If you can still
   get it, it's worth more than it looks. Kaggle gives Colab Pro accounts **+15 GPU h/week** (+30 for Pro+) when you
   link them [KNB]. That's +65 Kaggle h/mo (≈ +40 5090-eq h/mo at the ceiling). Check
   colab.research.google.com/signup while logged in.
5. **Cash-like credits that are poor for GPUs:**
   - GCP: $300 for 90 days, but no GPUs during the trial. After you upgrade, the leftover credit only covers
     "products and services covered by the Free Trial", and GPUs aren't in the Free Tier [GCPFREE]. Assume ~$0 of
     GPU value.
   - AWS: $100 + up to $100 over 6 months. The Free plan covers "select services" only [AWSFREE][AWSPLANS]. Whether
     GPU instances are eligible is UNVERIFIED.
   - Azure for Students: $100 for 12 months, no card [AZSTU]. Community Q&A reports zero GPU quota and no quota
     increases on student subscriptions [AZQA] (UNVERIFIED as official policy).

**Cash comparison:** $30 rents a real RTX 5090 on Vast.ai for ~61 h on-demand ($0.49/h) or ~95 h interruptible
($0.315/h) [VAST]. Per dollar, that's about the same as Modal's best GPU on FLOPs, and better for a small model.
Renting the whole fx2-T run (208 5090-h) costs about **$65–145**. The 2,800 h figure costs about **$880–1,930**
(§3.11).

---

## 1. Kaggle (recurring, weekly)

| Item | Value | Source |
|---|---|---|
| GPU quota | "resets weekly and is 30 hours or sometimes higher depending on demand and resources" | [KGPU] |
| GPU types | "1 Nvidia Tesla P100 GPU", or "T4 x2" = "2 Nvidia Tesla T4 GPUs" | [KNB] |
| TPU | "TPU v3-8", "up to 20 hours per week of TPUs and up to 9h at a time" | [KTPU][KNB] |
| Session length | "12 hours execution time for CPU and GPU notebook sessions and 9 hours for TPU notebook sessions" | [KNB] |
| Host, GPU sessions | 4 CPU cores, 29 GB RAM (both P100 and T4×2) | [KNB] |
| Host, CPU-only | 4 cores, 30 GB RAM | [KNB] |
| Host, TPU VM | 96 CPU cores, 330 GB RAM | [KNB] |
| Disk | 20 GB auto-saved `/kaggle/working`, plus scratch space that is not saved | [KNB] |
| Idle timeout | Interactive editor: "20 minutes of idle time" [KNB]. Interactive sessions "remain active until they reach the 60 minute idle timeout limit" [KGPU]. The two docs pages disagree | [KNB][KGPU] |
| Background execution | Yes. "Save & Run All" runs a fresh batch session top to bottom; "the entire Notebook must execute within 12 hours". You can also push versions via the Kaggle API without opening the editor | [KNB][KGPU] |
| Colab-link bonus | Colab Pro / Pro+ get "15 and 30 hours of extra GPU hours per week". It's a promotion that can end at any time | [KNB] |

Notes and open questions:

- **Session-hours vs GPU-hours.** It is UNVERIFIED whether a T4×2 session burns quota at 1× or 2×. Community
  reports say quota counts session time, so T4×2 is the better deal. Check it on the quota meter after a short run.
- **Concurrent GPU sessions.** The limit is UNVERIFIED (not in the docs). The quota is shared, so parallel sessions
  just drain it faster.
- **P100 is Pascal (sm_60), with no bf16 and no tensor cores.** Recent PyTorch CUDA 12.8+ wheels may have dropped
  Pascal support (UNVERIFIED). Use the T4×2 accelerator.
- **Older TPU material.** The TPU page warns that its docs were "written for earlier TPU versions that Kaggle no
  longer supports" [KTPU]. The Notebooks page still names v3-8 [KNB]. The current TPU generation is UNVERIFIED.
- **12 h sessions force checkpoint/resume.** Write checkpoints into `/kaggle/working` (20 GB) and chain them through
  notebook outputs used as inputs [KNB].

## 2. Modal (recurring, monthly $30)

Starter plan [MODALP]:

- $0 base fee, "$30 / month free credits", GPU concurrency 10, 100 containers, up to 3 seats.
- Region selection costs "1.15 - 1.75x base prices".
- The "3x" non-preemptible multiplier applies to "CPU and Memory" only.
- GPU functions are always preemptible ("subject to preemption by default"; `nonpreemptible` is not supported for
  GPU functions) [MODALPRE].
- Maximum function timeout is 24 h [MODALTO]. Training must checkpoint.
- No RTX 5090 or 4090 is offered [MODALP][MODALGPU].

Per-second list prices are from [MODALP]. Per-hour figures are my arithmetic (× 3600).

| GPU | $/s | $/h | Hours for $30 (GPU only) | Hours for $30 incl. 2 cores + 16 GiB host |
|---|---|---|---|---|
| T4 | 0.000164 | 0.590 | 50.8 | 36.9 |
| L4 | 0.000222 | 0.799 | 37.5 | 29.4 |
| A10 (A10G) | 0.000306 | 1.102 | 27.2 | 22.7 |
| L40S | 0.000542 | 1.951 | 15.4 | 13.8 |
| A100 40 GB | 0.000583 | 2.099 | 14.3 | 12.9 |
| A100 80 GB | 0.000694 | 2.498 | 12.0 | 11.0 |
| RTX PRO 6000 | 0.000842 | 3.031 | 9.9 | 9.2 |
| H100 (SXM5) | 0.001097 | 3.949 | 7.6 | 7.2 |
| H200 (SXM) | 0.001261 | 4.540 | 6.6 | 6.3 |
| B200 | 0.001736 | 6.250 | 4.8 | 4.6 |
| B300 | 0.001972 | 7.099 | 4.2 | 4.1 |

- **CPU:** $0.0000131 per physical core per second, which is $0.0472/core-h. One physical core = 2 vCPU, minimum
  0.125 core [MODALP].
- **Memory:** $0.00000222 per GiB per second, which is $0.0080/GiB-h [MODALP].
- **Host overhead:** CPU and memory are billed on top of the GPU [MODALP]. The last column assumes a modest
  2-core / 16 GiB host = $0.222/h (ESTIMATE). On a T4 that overhead is 38% of the GPU price.

## 3. Other free and student compute

R = recurring, O = one-time, X = closed or not usable for GPU training.

### 3.1 Google Colab, free tier (R, unquantified)

- "access to expensive resources like GPUs is heavily restricted". Limits are not published "because they can vary
  over time".
- "notebooks can run for at most 12 hours, depending on availability and your usage patterns". Idle runtimes time
  out [COLABFAQ].
- GPU model not stated (users report a T4; UNVERIFIED).
- Not plannable. Use it for debugging, not budget.
- Colab Pro for Education: see §0 item 4.

### 3.2 Lightning AI (O)

- Free plan: 5 credits at registration + 25 when you add a card, "Up to 30 credits to start". Credits expire after
  12 months.
- "Get up to 50 free GPU hours" on interruptible machines.
- 1 GPU per Studio. Free Studio restarts every 4 h. A100/H100/H200 sessions are limited to 4 h; T4/L4/L40S sessions
  are unlimited.
- 50 GB storage [LIGHT].
- List prices: T4 $0.55–0.88/h, L4 $0.79–1.25/h, L40S $2.97–4.21/h, A100 40 GB $2.19/h, H100 $5.45–5.82/h,
  H200 $4.50/h, B200 $9.86/h [LIGHT].
- No recurring monthly free credit is shown (a third-party source says "Monthly credit: Not confirmed" [GPH]).

### 3.3 Amazon SageMaker Studio Lab (X for you)

- "no longer open to new customers" [SMSL].
- Existing users: GPU "up to 4 hours at a time with a limit of 4 hours in a 24-hour period"; 15 GB storage, 16 GB
  RAM [SMSLO].
- GPU type not stated.

### 3.4 Paperspace / DigitalOcean Gradient (R, weak)

- Free plan: "Auto-shutdown (12 hour limit)", "5GB storage", "Basic instances", public projects only [PSP].
- The free GPU model is not named on the official page.
- A third-party source says the free GPU is a Quadro M4000 (Maxwell, 8 GB) with a 6 h auto-shutdown [GPH]. Both
  figures are UNVERIFIED and conflict with the official 12 h.
- Useless for transformer training either way.
- Pro is $8/mo with "Faster free GPUs (based on availability)"; which GPUs is UNVERIFIED [PSP].

### 3.5 GitHub Student Developer Pack (mixed)

The pack page lists [GHSP]:

- Azure $100 credit (18+).
- Visual Studio Dev Essentials: "$200 credit for the 1st month".
- Heroku $13/mo for 24 months (no GPU).
- **Camber student plan: 40 CPU h, 5 GPU h, 50 GB storage per month** while enrolled (R). The GPU type is
  UNVERIFIED.
- **DigitalOcean credit is no longer on the pack page** [GHSP]. AWS, GCP, Modal and Lightning are not listed either.

### 3.6 Azure for Students (O, likely no GPU)

- $100 "within 12 months on most Azure products".
- No credit card. Full-time university students. Renewable yearly while a student [AZSTU].
- The page says nothing about GPU VMs.
- Microsoft Q&A threads say Azure for Students subscriptions have 0 N-series quota and are not eligible for quota
  increases; upgrading to pay-as-you-go is the route [AZQA]. UNVERIFIED as official policy.

### 3.7 Google Cloud free trial (O, no GPU)

- $300 Welcome credit, "valid for 90 days".
- During the trial you can't "Add GPUs to your VM instances". After you upgrade, GPUs are unlocked.
- Remaining credit covers only "products and services covered by the Free Trial", and "GPUs and TPUs are not
  included in the Free Tier offer" [GCPFREE].
- Whether leftover credit pays for GPU-hours after upgrading is UNVERIFIED. Assume no.

### 3.8 AWS (O)

- New accounts get $100 plus up to $100 for completing activities, over at most 6 months [AWSFREE][AWSPLANS].
- The Free plan includes only "select AWS services". It "closes when credits are depleted or when the plan duration
  ends".
- GPU (G/P) instance eligibility on the Free plan is UNVERIFIED. New-account GPU vCPU quotas are also UNVERIFIED
  (community reports say they are often 0).
- AWS Educate: not found on the current Free Tier page (UNVERIFIED whether it still grants credits).

### 3.9 Google TPU Research Cloud (O / renewable on request)

- Still listed. Access to "a cluster of more than 1,000 Cloud TPU devices" at no charge [TRC].
- Apply via the "Express interest" form (https://forms.gle/uwWjUewyxZ5okL3W7). "Invitations are sent out to approved
  applicants on a rolling basis" [TRC][TRCFAQ].
- "Anyone can express interest".
- Expectations: share TRC-supported research publicly (paper, open-source code, or blog post) and give feedback
  [TRC][TRCFAQ].
- TPUs are free; small VMs and GCS buckets are billed ("generally minimal") [TRCFAQ].
- Quota size, TPU generation and duration are not stated (the common "30 days" figure is UNVERIFIED).
- TRC quota is "not compatible with CMLE / CAIP / Vertex" [GHTRC]. The official GitHub repo was archived 19 Apr 2026
  [GHTRC].
- An open-source Hutter entry fits the publication expectation well. The catch is the TPU port of the KDA layers.

### 3.10 Hugging Face ZeroGPU / Spaces (R, unusable for training)

- Daily quota: free account 5 min, PRO 40 min. Resets 24 h after first use [HFZG].
- Hardware: half an RTX PRO 6000 Blackwell (48 GB) by default; the full GPU uses 2× quota [HFZG].
- Gradio SDK only, 60 s default per call, no `torch.compile` [HFZG].
- PRO can buy extra time at "$1 per 10 minutes" ($6/h) [HFZG].
- Not a training platform.

### 3.11 Marketplace prices for comparison (cash)

- **Vast.ai:** queried via the public offers API at 2026-11-04 ~20:40 local time [VAST]. Filters: cheapest verified
  1-GPU offers, `dph_total` including host markup. Storage and bandwidth are extra.
- **RunPod:** on-demand list prices from the pricing page [RUNPOD]. No spot tier is shown.
- **TensorDock:** "from" prices on the homepage [TD]. No dedicated pricing page (`/pricing` returned 404).

| GPU | Vast.ai on-demand (cheapest) | Vast.ai interruptible (min bid) | RunPod Community / Secure | TensorDock "from" |
|---|---|---|---|---|
| RTX 4090 | $0.32 | $0.22 | $0.34 / $0.89 | $0.35 |
| RTX 5090 | $0.49 | $0.31 | $0.69 / $1.19 | not listed |
| A100 PCIe | $0.40 | $0.30 | $1.19 / $1.79 (80 GB) | — |
| A100 SXM4 | $0.31 | $0.27 | $1.39 / $1.79 (80 GB) | $1.80 |
| H100 PCIe | $2.54 | $1.33 | $1.99 / $2.89 | — |
| H100 SXM | $2.27 | $0.77 | $2.69 / $3.99 | $2.25 |

- Vast prices are a live snapshot of a few listings and move hourly.
- RunPod: no free or student credit is shown. A third-party source mentions a $5–500 random referral bonus after a
  $10 load [GPH] (UNVERIFIED).

**Cash cost of the reference runs (ESTIMATE):**

| Run | Vast 5090 interruptible ($0.315/h) | Vast 5090 on-demand ($0.49/h) | RunPod Community ($0.69/h) |
|---|---|---|---|
| fx2-T, 208 5090-h | $66 | $102 | $144 |
| neif, ~2,800 5090-h | $882 | $1,372 | $1,932 |

### 3.12 Other

- **Camber:** see §3.5.
- **Deepnote:** student Team plan, CPU only (5 GB RAM, 2 vCPU) [GHSP].
- **Saturn Cloud:** no free GPU tier is stated; pay-as-you-go T4 from $0.15/h [SATURN].

## 4. Normalized table

Dense bf16 tensor TFLOPS from vendor spec sheets. T4 and P100 lack bf16, so their FP16 figure is used.

| Source | GPU | Dense TFLOPS | Spec source | Free h / month | Ratio vs 5090 | Free 5090-eq h / mo | Cost/h beyond free |
|---|---|---|---|---|---|---|---|
| reference | RTX 5090 | **209.5** (BF16, FP32 acc.) | [RTXBW] Table 3 | — | 1.000 | — | Vast $0.31–0.49, RunPod $0.69 |
| reference | RTX 4090 | 165.2 (BF16, FP32 acc.) | [RTXBW] Table 3 | — | 0.789 | — | Vast $0.22–0.32, RunPod $0.34 |
| Kaggle | T4 ×2 | 2 × 65 = 130 (FP16 mixed) | [T4] | 130 (session-h) | 0.621 | 80.7 (ceiling) | n/a (no paid tier) |
| Kaggle | P100 16 GB | 18.7 (FP16, no tensor cores) | [P100] | same pool | 0.089 | 11.6 if all P100 | n/a |
| Kaggle | TPU v3-8 (4 chips) | 4 × 123 = 492 (BF16) | [TPUV3] | 86.7 | 2.348 | 203.5 (needs a port) | n/a |
| Modal | T4 | 65 | [T4] | 50.8 | 0.310 | 15.8 | $0.590 |
| Modal | L4 | 121 (242 sparse / 2) | [L4] | 37.5 | 0.578 | 21.7 | $0.799 |
| Modal | A10 | 125 | [A10] | 27.2 | 0.597 | 16.2 | $1.102 |
| Modal | L40S | 362.05 | [L40S] | 15.4 | 1.728 | 26.6 | $1.951 |
| Modal | A100 40/80 GB | 312 | [A100] | 14.3 / 12.0 | 1.489 | 21.3 / 17.9 | $2.099 / $2.498 |
| Modal | RTX PRO 6000 | ≈ 500 (ESTIMATE, see note) | [PRO6000] | 9.9 | ≈ 2.39 | ≈ 23.6 | $3.031 |
| Modal | H100 SXM | 989.5 (1,979 sparse / 2) | [H100] | 7.6 | 4.723 | 35.9 | $3.949 |
| Modal | H200 | 989.5 (1,979 sparse / 2) | [H200] | 6.6 | 4.723 | 31.2 | $4.540 |
| Modal | B200 | 2,250 (36 PF sparse / 8 GPUs / 2) | [HGX] | 4.8 | 10.74 | 51.6 | $6.250 |
| Lightning | interruptible (type UNVERIFIED) | — | — | ~50 h once | — | — | T4 from $0.55 |
| HF ZeroGPU | ½ RTX PRO 6000 | ≈ 250 (ESTIMATE) | [HFZG][PRO6000] | 2.5 (5 min/day) | ≈ 1.2 | ≈ 3 (not usable for training) | $6/h (PRO credits) |
| Camber | UNVERIFIED | — | — | 5 | — | — | — |

- Free hours for Modal are each GPU's $30 alone (choose one GPU type per month). Free 5090-eq hours exclude host
  CPU/RAM; see §2 for net figures.
- Ratio = GPU TFLOPS / 209.5.
- **RTX PRO 6000 note:** NVIDIA's page lists only "4000 TOPS" FP4 (sparse) and 125 TFLOPS FP32 [PRO6000]. ≈ 500
  dense BF16 assumes FP4 dense = 2,000 and BF16 = FP4 / 4 with full-rate FP32 accumulate. UNVERIFIED.
- **Sparse vs dense:**
  - NVIDIA's L4, H100, H200 and HGX pages headline sparse figures. The footnotes say dense is half [L4][HGX], or mark
    the figure "with sparsity" [H100][H200].
  - The 5090 figure is the dense half of "209.5/419" [RTXBW].
  - The 5090's FP16-accumulate rate (419 dense) is double, but training uses FP32 accumulate.

## 5. Reference conversions

### 5.1 Hours of our compute per RTX-5090-hour (FLOP ratio, ESTIMATE)

`hours per 5090-h = 209.5 / TFLOPS(GPU)`:

| GPU | Calculation | Hours per 5090-hour |
|---|---|---|
| Kaggle T4 ×2 (one session) | 209.5 / 130 | 1.61 session-h |
| Single T4 | 209.5 / 65 | 3.22 |
| Kaggle P100 | 209.5 / 18.7 | 11.2 |
| Kaggle TPU v3-8 | 209.5 / 492 | 0.43 |
| Modal L4 | 209.5 / 121 | 1.73 |
| Modal A10 | 209.5 / 125 | 1.68 |
| Modal L40S | 209.5 / 362.05 | 0.58 |
| Modal A100 | 209.5 / 312 | 0.67 |
| Modal H100 / H200 | 209.5 / 989.5 | 0.21 |
| Modal B200 | 209.5 / 2,250 | 0.093 |

### 5.2 fx2-cmix-transformer: 8 × RTX 5090 × 26 h = 208 GPU-h

The figure is from [FX2T] via docs/LANDSCAPE.md. It is a single final training run: 320 bf16 + 256 QAT + 16 fp32
epochs.

| Budget | Calculation | Months |
|---|---|---|
| Ceiling, Kaggle T4×2 + Modal B200 | 208 / (80.7 + 49.8) = 208 / 130.5 | **1.6** |
| Realistic, Kaggle 1×T4 + Modal H100 | 208 / (40.3 + 34.0) = 208 / 74.3 | **2.8** |
| Kaggle T4×2 only | 208 / 80.7 | 2.6 (208 × 1.61 = 335 session-h, about 28 twelve-hour sessions) |
| Modal $30 only, B200 | 208 / 49.8 | 4.2 |
| Kaggle P100 only | 208 / 11.6 | 17.9 |

### 5.3 cmix-neif: about 2,800 GPU-h, "mostly RTX 5090"

The figure is from [NEIF] via docs/LANDSCAPE.md ("about €4,000 of compute including CPU testing"). It is the whole
development effort (1,000 bf16 epochs in a warm-started chain + QAT + fp), not one clean run. Treated here as
2,800 5090-h.

| Budget | Calculation | Months |
|---|---|---|
| Ceiling | 2,800 / 130.5 | **21.5** |
| Realistic | 2,800 / 74.3 | **37.7** |
| Kaggle T4×2 only | 2,800 / 80.7 | 34.7 |
| Modal $30 only, B200 | 2,800 / 49.8 | 56.2 |

### 5.4 Why these are only FLOP-ratio estimates

- **Memory bandwidth.**
  - 5090: 1,792 GB/s [RTXBW].
  - T4: "320+ GB/s" [T4]; ~5.6× less, versus a 3.2× FLOP gap.
  - L4: 300 GB/s [L4]. A10: 600 GB/s [A10]. L40S: 864 GB/s [L40S].
  - A ~6–8M-param, d=192 model with small matmuls is likely bandwidth- and launch-bound, so slow-memory GPUs (T4,
    L4) will do worse than their FLOP ratio.
  - Big GPUs (H100, B200) will be badly under-filled, so their FLOP ratio flatters them most.
- **No bf16 on T4 and P100.**
  - T4 lists FP16 mixed precision only [T4]. P100 has FP16 without tensor cores [P100].
  - The record recipes train in bf16 [FX2T][NEIF]. On T4 that means fp16 with loss scaling (a numerics risk for QAT)
    or fp32 at 8.1 TFLOPS [T4], 26× slower than the 5090's bf16 rate.
  - P100 is effectively only for smoke tests.
- **Multi-GPU scaling.** fx2-T used 8 GPUs in one box. Kaggle T4×2 has no NVLink, and a model this small may get
  well under 2× from data parallelism. The "realistic" column already assumes 1×.
- **Preemption and session caps.** Modal GPU functions are always preemptible with a 24 h cap [MODALPRE][MODALTO].
  Kaggle caps sessions at 12 h [KNB]. Checkpoint/restart overhead and queueing are not counted.
- **Custom kernels.** KDA/sliding-window kernels may target recent architectures. T4 is Turing (sm_75) and P100 is
  Pascal. Kernel availability on these GPUs is UNVERIFIED.

**Bottom line:**

- Free recurring compute is enough for small-scale experiments: prefix-trained models and ablations at 1–10% of a
  full run.
- It is enough to reproduce one fx2-T-class training in ~3 months.
- It is **not** enough for neif-scale iteration.
- The cash path ($65–145 per fx2-T-scale run on rented 5090s) or a one-time academic/TRC grant is what makes
  full-scale A-direction work feasible.

---

## Sources

All retrieved 2026-11-04.

- **[KNB]** Kaggle Notebooks docs (Technical Specifications; Colab Pro section), https://www.kaggle.com/docs/notebooks
  (JS-rendered; read via browser).
- **[KGPU]** Kaggle Efficient GPU Usage Tips, https://www.kaggle.com/docs/efficient-gpu-usage
- **[KTPU]** Kaggle TPU docs, https://www.kaggle.com/docs/tpu
- **[MODALP]** Modal pricing, https://modal.com/pricing
- **[MODALGPU]** Modal GPU guide, https://modal.com/docs/guide/gpu
- **[MODALPRE]** Modal preemption guide, https://modal.com/docs/guide/preemption
- **[MODALTO]** Modal timeouts guide, https://modal.com/docs/guide/timeouts
- **[COLABFAQ]** Colab FAQ, https://research.google.com/colaboratory/faq.html
- **[COLABEDU]** Google blog, "New Google Colab features for higher education" (18 Jul 2025),
  https://blog.google/products-and-platforms/products/education/colab-higher-education/
- **[LIGHT]** Lightning AI pricing, https://lightning.ai/pricing
- **[SMSL]** SageMaker Studio Lab, https://docs.aws.amazon.com/sagemaker/latest/dg/studio-lab.html
- **[SMSLO]** Studio Lab components overview, https://docs.aws.amazon.com/sagemaker/latest/dg/studio-lab-overview.html
- **[PSP]** Paperspace pricing, https://www.paperspace.com/pricing
- **[GHSP]** GitHub Student Developer Pack, https://education.github.com/pack
- **[AZSTU]** Azure for Students, https://azure.microsoft.com/en-us/free/students
- **[AZQA]** Microsoft Q&A (community answers, not policy), e.g.
  https://learn.microsoft.com/en-us/answers/questions/2141487/request-for-gpu-enabled-vm-quota-increase-for-azur
  and https://learn.microsoft.com/en-us/answers/questions/2157302/clarification-on-gpu-access-after-upgrading-azure
- **[GCPFREE]** Google Cloud free program, https://docs.cloud.google.com/free/docs/free-cloud-features
- **[AWSFREE]** AWS Free Tier, https://aws.amazon.com/free/
- **[AWSPLANS]** AWS "Choosing a plan", https://docs.aws.amazon.com/awsaccountbilling/latest/aboutv2/free-tier-plans.html
- **[TRC]** TPU Research Cloud, https://sites.research.google/trc/about/
- **[TRCFAQ]** TRC FAQ, https://sites.research.google/trc/faq/
- **[GHTRC]** https://github.com/google-research/trc (archived 19 Apr 2026; seen via search result summary,
  UNVERIFIED directly)
- **[HFZG]** Hugging Face ZeroGPU docs, https://huggingface.co/docs/hub/spaces-zerogpu
- **[VAST]** Vast.ai public offers API, `https://console.vast.ai/api/v0/bundles/?q=...` (verified, rentable, 1 GPU,
  sorted by `dph_total`; on-demand and bid queries), ~20:40 local time
- **[RUNPOD]** RunPod pricing, https://www.runpod.io/pricing
- **[TD]** TensorDock homepage, https://www.tensordock.com/
- **[SATURN]** Saturn Cloud plans, https://saturncloud.io/plans/saturn_cloud_plans/
- **[GPH]** Secondary, used only where marked: "Free Cloud GPUs in 2026", https://gpuperhour.com/blog/free-cloud-gpus-and-credits
  (its own figures read 21 Sep 2026)
- **[RTXBW]** NVIDIA RTX Blackwell GPU Architecture whitepaper, Appendix Table 3,
  https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf
- **[T4]** https://www.nvidia.com/en-us/data-center/tesla-t4/
- **[P100]** Tesla P100 PCIe datasheet, https://images.nvidia.com/content/tesla/pdf/nvidia-tesla-p100-PCIe-datasheet.pdf
- **[L4]** https://www.nvidia.com/en-us/data-center/l4/
- **[A10]** https://www.nvidia.com/en-us/data-center/products/a10-gpu/
- **[A100]** https://www.nvidia.com/en-us/data-center/a100/ (80 GB figures; 40 GB assumed equal at 312 dense,
  UNVERIFIED on this page)
- **[L40S]** https://www.nvidia.com/en-us/data-center/l40s/
- **[H100]** https://www.nvidia.com/en-us/data-center/h100/
- **[H200]** https://www.nvidia.com/en-us/data-center/h200/
- **[HGX]** https://www.nvidia.com/en-us/data-center/hgx/ (HGX B200/B300: 36 PFLOPS FP16/BF16 sparse per 8 GPUs)
- **[PRO6000]** https://www.nvidia.com/en-us/products/workstations/professional-desktop-gpus/rtx-pro-6000/
- **[TPUV3]** Cloud TPU v3, https://docs.cloud.google.com/tpu/docs/v3
- **[FX2T]**, **[NEIF]**: see docs/LANDSCAPE.md sources.
