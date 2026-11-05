# Hutter Prize (enwik9, 500,000 EUR): rules and constraints

All facts below were read directly from the primary pages on **2026-11-04**. Unless noted otherwise:

- **[R]** = http://prize.hutter1.net/hrules.htm (HTTP `Last-Modified: Wed, 09 Oct 2024`). This is the authoritative rules page.
- **[M]** = http://prize.hutter1.net/ (main page, `Last-Modified: Wed, 30 Sep 2026`).
- **[F]** = http://prize.hutter1.net/hfaq.htm (FAQ, `Last-Modified: Wed, 30 Sep 2026`).
- **[LTCB]** = https://mattmahoney.net/dc/text.html (Matt Mahoney's Large Text Compression Benchmark, "Last update: Oct. 14, 2026"). Matt Mahoney runs the prize testing (see [M], "Committee").

All were retrieved 2026-11-04. Anything marked **UNVERIFIED** is my interpretation or comes from a secondary source and is not stated outright on the official pages.

---

## TL;DR: constraints checklist

> **The number to beat.** The current record is **L = 100,424,672 bytes** (Vladimir Ivanov, fx2-cmix-transformer, submitted 24 Jul 2026, awarded 30 Sep 2026) [M].
> A prize needs at least a 1% improvement: **S < 99,420,425 bytes** [M].
> **Our goal of S < 100,000,000 is only 0.42% below L. On its own it does NOT qualify for a prize, and it would not become the new L** [R].
> **Watch out:** pending entries listed on [LTCB] already claim S ≈ 99.31M (zmix, lexth11c) and S = **95,914,051** (cmix-neif). If those are verified, L drops to about 95.9M, and sub-100M will already be beaten (see §5).

Hard limits we have to stay within. Each one is cited in the sections below.

- [ ] **Score:** S = size(compressor `comp9`, as an executable or as a zip of its source) + size(self-extracting `archive9`). The compressor counts. The decompressor counts inside the archive, or separately (×2, or ×1 if it is the same binary as the compressor) [R].
- [ ] **Lossless:** running `archive9` with no input must write a file byte-identical to enwik9 (10^9 bytes) [R].
- [ ] **Time:** each program (compressor and decompressor, separately) must finish in **< 70,000 / T hours**, where T is the machine's **Geekbench 5** score. On the reference machines that works out to **about 49 h** (Intel, T≈1427) and **about 53 h** (AMD, T=1310). Plan for **≤ ~45 h on one core** [R][M].
- [ ] **One CPU core.** No multithreading speedup counts [M][F].
- [ ] **No GPU** at all, for compression or decompression [R]. (GPUs are fine for *offline* development and training, but any resulting weights must be shipped inside the counted binaries [F][LTCB].)
- [ ] **RAM ≤ 10 GB** [R]. Treat this as 10^10 bytes peak RSS, with some margin (see §2).
- [ ] **Scratch disk ≤ 100 GB** of temporary files [R].
- [ ] **Platform:** Windows or Linux, x86 32-bit or 64-bit executables. No additional installations. Must actually run on **both** test machines: an Intel i7-1165G7 under Windows (Linux under WSL2 with about 12 GB visible) and an AMD Ryzen 7 (3700X, Zen 2: **no AVX-512**) under Linux [R][LTCB].
- [ ] **No external input:** no network, no external files or dictionaries. Standard libraries such as file I/O are OK. Any dictionary, model or weights must be inside the counted executables or archive [R][F].
- [ ] **Languages:** C++, Python or assembler are accepted; others are "considered" [R]. Executables, or zipped source plus makefile, are accepted. **Any required command-line options are added to S** [R].
- [ ] **Open source:** documented source under an OSI-approved license, a write-up of the algorithm, and free public download. All of this is required before payout [R][F].
- [ ] **Process:** email the itemized submission to all 3 committee members. There is a **≥ 30-day public comment/verification period**. Submissions are processed in order received. If you beat your own record within 30 days, the older entry is withdrawn [R].

---

## 1. What is measured (S) and submission format

### 1.1 Default form: compressor plus self-extracting archive
From [R], "Rules":
- "Publish a compression program `comp9.exe` that outputs `archive9.exe` given input `enwik9`."
- "If `archive9.exe` is run with no input, it reproduces 10^9 byte file `data9` that is identical to `enwik9`."
- "**Total size is measured as S := length(comp9.exe/zip) + length(archive9.exe).**"
- "The used filenames (except enwik9) are just for illustration." (This line is in a commented-out part of the HTML source of [R]. Recent entries do name the SFX `archive9` with no `.exe` [LTCB, zmix 1.0.1], so names are clearly flexible.)

So **the compressor's size counts**, and the decompressor's size counts because it is part of `archive9`. [M] says the same thing: "Create a Linux or Windows compressor comp.exe of size S1 that compresses enwik9 to archive.exe of size S2 such that S:=S1+S2 < L".

### 1.2 Relaxation: separate decompressor
From [R], "Relaxations":
- Instead of `comp9.exe` you may submit a compressor `comp9a.exe` that produces `archive9.bhm`, plus a decompressor `decomp9.exe` that turns `archive9.bhm` into the data. In that case
  **S := length(comp9a.exe/zip) + 2×length(decomp9.exe/zip) + length(archive9.bhm).**
- "If `comp9a.exe` = `decomp9.exe`, the 2× can be reduced to 1×." This sentence was **added in 2024**: it is absent from the 2023-01-01 Wayback snapshot of [R] and present from the 2024-11-28 snapshot onward. The rationale is in [F]#addcomp.
- "Resource restrictions for all executables are the same."

Rationale ([F]#addcomp): counting the compressor stops people from hiding precomputed work in it. The 2× on a separate decompressor stops people from moving the archive into the decompressor. Large hand-built tables placed in both C and D are effectively counted 2 to 3 times: "If you need large (word) tables, write a short program that creates them from enwik9 itself."

### 1.3 How program size is counted
- `length(X.exe/zip)` [R]: either the executable, or (as allowed in "Relaxations") "zip-files X.zip containing the source code and makefile, which create X.exe".
- Executables may be packed with UPX ([F]#self: "You can compress your executable with programs like UPX, because the result is still an executable"). Past winners do this. For example, the cmix-lex compressor was "compressed with UPX to 459,938 bytes", and its official compressor size was 470,599 [LTCB #1091].
- "If command-line options for execution or compilation are necessary, their length is added to S." [R]. Example: cmix-neif lists its compressor size as "4,000,689+19" [LTCB #0919]. My reading is that the +19 is option bytes (**UNVERIFIED** interpretation).
- Pre-trained weights embedded in the binaries count in full, **and they appear twice**, once in the compressor and once in the SFX archive. fx2-cmix-transformer's 6M-parameter transformer adds "2.9 MB to the compressor and decompressor" [LTCB #0969]. The zmix writeup says "The score counts the weights twice, once in the compressor and once in the archive" [LTCB #0960]. In the record entry, the compressor alone is 3,428,474 bytes.

### 1.4 Required submission contents
Email to **mattmahoneyfl@gmail.com, jabowery@gmail.com and marcus.hutter@gmx.net** with this numbered list [R], "Participation":
1. A direct link to `archive9.exe` (or `decomp9.exe` + `archive9.bhm`).
2. A **single line** of instruction on how to execute it. "Long/complicated instructions are inadmissable."
3. Names of all programs involved, their versions, and any options used.
4. Sizes of (de)comp9(a) and of archive9.exe/bhm.
5. Approximate (de)compression time and maximum RAM and disk used.
6. A description of the test machine: CPU, memory, OS, **Geekbench5 score**.
7. Download links for: executables (or zipped source) and well-documented source code under an OSI license, plus all other relevant files; a document explaining the algorithmic ideas; usage/compilation instructions.
8. Any additional info.

Other points from [R], "More Information":
- Programs may be tuned specifically to enwik9 and may fail on any other input.
- Only the submitted version and options are eligible.

---

## 2. Hardware and compute limits

### 2.1 The rule text
From [R]:
> "Each program must run in less than **70'000/T hours** on a machine using at most **10GB RAM** and **100GB HDD** for temporary files, where T is the machine's **Geekbench5** score. **No GPU usage.**
> In particular they must run on our current test machines, which are as of 2021 (but may change without notice)
> a **Lenovo 82HT Intel Core i7-1165G7 2.79GHz (Windows)** with T ≈1427 (1 core) and T ≈4667 (4 cores)
> and an **AMD Ryzen 7 3.6GHz (Linux)** with T =1310 (1 core) and T =8228 (8 cores)."

From [M], "The Task":
> "Restrictions: Must run in **≲50 hours using a single CPU core** and <10GB RAM and <100GB HDD on our test machine."

### 2.2 Derived time budget (single-core T)
| Test machine | Single-core GB5 T | 70,000/T |
|---|---|---|
| Intel i7-1165G7 (Windows; Linux runs via WSL2) | ≈1427 | **≈49.1 h** |
| AMD Ryzen 7 3.6 GHz (Linux) | 1310 | **≈53.4 h** |
| Your own machine | T_yours | 70,000 / T_yours |

- The limit applies to **each program separately**: the compressor and the decompressor each get the full budget [R] ("Each program must run ...").
- **Use the single-core T.** [M] and [F]#cpucore say the limit is for "a single CPU core". Mahoney applies the formula with single-core scores, e.g. "Both are within the Hutter prize limits of 10 GB and 70000/1648 = 42.475 hours" for a Ryzen 9 5900X with single-core GB5 1648 [LTCB #1091].
- To estimate on your own hardware, look up its Geekbench 5 single-core score (https://www.geekbench.com/legacy/, linked from [R]) and keep the run comfortably under 70,000/T. [R] also suggests running previous record holders on your machine and comparing their runtimes with the published ones.
- **Stale or inconsistent text (ignore it, and follow [R]):** [F]#cpucore still says "100 hours on one i7 core ... roughly corresponds 500'000/T hours", and [F]#ram1gb talks about "less than 100 hours". These are leftovers from the earlier "100 h / 50,000/T" rule, which is now commented out in the HTML source of [R].
- **What actually happens (observation from [LTCB], not a rule):** the record entry fx2-cmix-transformer took 133,766 ns/byte ≈ **37.2 h** to compress on the AMD machine. Its Intel run took **8 days** "because only 12 GB are available to Linux under WSL2 on the test machine for memory mapping the PPM model to disk", and the entry was still awarded [LTCB #0969]. It is **UNVERIFIED** exactly which machine's timing the committee uses as binding. **Design for ≤ ~45 h single-core on the AMD 3700X class, and make sure it also runs correctly on the Intel box.**

### 2.3 Memory, disk, cores, GPU
- **RAM: 10 GB** [R]. The rules do not say whether "GB" means 10^9 or 2^30 bytes (**UNVERIFIED**). Mahoney described starlit as using "less than 10 GiB" [LTCB #1149], while the FAQ uses SI units for enwik9 [F]#1GB. **Be conservative: keep peak RSS ≤ ~9.5×10^9 bytes.** Recent entries run at about 9.4 to 10.0 GB and push the limit by memory-mapping large PPM models to disk [LTCB #0969, #1091, #1137].
- **Disk: 100 GB** of temporary files [R]. Past entries used about 17 to 45 GB of scratch space [LTCB #1149, #1137].
- **Cores: 1.** "Why do you restrict to a single CPU core and exclude GPUs?" [F]#cpucore. "If we'd allow 100 hours straight, we'd favor super computers with 1000s of cores." Multithreading gives no benefit within the rules, and running several threads concurrently is effectively disallowed (**UNVERIFIED** whether a helper I/O thread is tolerated. Mahoney notes that existing entries "run in 1 thread" [LTCB #1137]).
- **GPU: not allowed** at runtime [R] ("No GPU usage"), [F]#cpucore. Offline GPU training of weights that are then shipped inside the binaries **is accepted in practice**. FAQ: "Pre-trained medium-sized Transformers are fast enough by externalizing the high compute demand while still being small enough to be competitive" [F]#largenn. The record entry was "pre-trained on enwik9 on 8 RTX 5090 GPUs for 26 hours ... It does not need a GPU to run" [LTCB #0969].
- **CPU ISA:** the AMD test machine is a Ryzen 7 3700X (Mahoney: "AMD Ryzon 7-3700x, 3.6 GHZ, 8 cores, 16 threads, 64 GB" [LTCB #1137]). That is Zen 2, which supports AVX2 but **not AVX-512** (CPU fact, not from the prize pages). The Intel machine is Tiger Lake (i7-1165G7: "4 cores, 8 threads, 16 GB under Ubuntu in Windows 11/WSL" [LTCB #1137]). **Target x86-64 + AVX2, and test on both Intel and AMD.** fx2-cmix-transformer initially "gave a segmentation fault on an Intel Core-i7 1165G7" and needed a fix [LTCB #0969]. Some winners submitted separate AMD and Intel builds and archives [LTCB #0969, #1080] (**UNVERIFIED** whether that is formally allowed. It is what happened in practice).
- **Floating-point determinism across CPUs** matters, because compression and decompression may run on different machines. fx-deepmix mentions "floating-point and state-reset fixes intended to improve deterministic reproduction across compatible systems" [LTCB #1078].

---

## 3. OS, languages, toolchains, network, external data

- **OS/format:** "Programs must be Windows or Linux (x86 32bit or 64bit) executables." [R]. Rationale in [F]#exe.
- **Self-contained:** "Programs must run without input from other sources (files, network, dictionaries, etc.) under Windows or Linux without additional installations. Use of standard libraries as for file I/O are allowed." [R]
- **Languages:** "C++, Python, and Assembler are accepted. We may also consider other languages if we can easily compile and run and verify the code for compliance with the other rules." [R]. A Zig-based entry (zmix) is pending [LTCB #0960]. Whether it is accepted is **UNVERIFIED**.
- **Source instead of binary:** allowed as "zip-files X.zip containing the source code and makefile, which create X.exe" [R].
- **Network:** forbidden. "decompressors (secretely) receiving any kind of 'outside' information are forbidden." [R], [M] "Disclaimer".
- **External data and dictionaries:** not allowed as separate inputs [R]. [F]#background ("Why don't you allow using some fixed default background knowledge database?") says to derive the knowledge from enwik9 itself. [F]#self: "External needed data files (like dictionaries) should be added to the decompressor". In other words, embed them and pay for their bytes. Pre-trained weights are allowed in this embedded, counted form ([F]#largenn, [F]#getsource: "winner Vladimir Ivanov externalized the high compute demand by pre-training a 6M-parameter Transformer ... off-line").
- **Help with portability:** "If your compressor beats the current record, but violates some of the constraints regarding operating system, used dll's, used programming language, etc, Matt Mahoney may be willing to assist you" [R].
- Rules may change "at any time without notice", and Marcus Hutter makes the final decision. "Payment of the prize cannot be legally enforced." [R]

---

## 4. Prize formula and thresholds

From [R], "Award":
- **Award = Z × (L − S) / L**, where S is the new record, L is the previous record, and **Z = 500,000 EUR**.
- "Update: L := S, while Z itself does not get reduced." Z is constant [R], [F]#money.
- "**Minimum award is 1% of Z**", i.e. **5,000 EUR** [R]. [M]: "Minimum claim is 5'000€ (1% improvement)."
- "a miss of the 1% criterion will not diminish the prize (**L remains unchanged**)." [R]. So a sub-1% improvement earns nothing **and does not reset L**.
- Committee members are not eligible for money, but their entries still update L [R].
- With multiple authors, the submission must include instructions for dividing the money [R].
- [R] still says "1€ for every ~230 byte improvement, with a minimum improvement of ~1MB". That is stale. At the current L it is **L/Z ≈ 200.8 bytes per EUR**, with a minimum improvement of **1,004,247 bytes**.

**Current numbers** (L from [M]):
| Quantity | Value |
|---|---|
| L (current record) | **100,424,672** |
| Max S to claim the minimum prize (L × 0.99) | **< 99,420,425** ([M] lists "<99'420'425") |
| Payout at S = 99,420,425 | ≈ 5,000 EUR |
| Payout at S = 95,000,000 | ≈ 27,009 EUR |
| Payout at S = 100,000,000 (our goal) | **0 EUR** (only a 0.42% improvement, below the 1% minimum, and L stays unchanged) |

The 2021 "5,000 bytes per day" relaxation was temporary and has ended. It is commented out on [M] ([F]#ddisc).

---

## 5. Records

### 5.1 enwik9 record table (from [M], "Current and Previous Records")
| Date (submitted) | Author(s) | Program | Total S (bytes) | Improvement | Award |
|---|---|---|---|---|---|
| 24 Jul 2026 | **Vladimir Ivanov** | **fx2-cmix-T** (fx2-cmix-transformer) | **100,424,672** | 7.46% | 37,300 EUR |
| 19 Jul 2026 | David Freelan | cmix-obias | 108,521,870 | 1.05% | 5,240 EUR |
| 26 Jun 2026 | Ibrahim Marcouch & Kaido Orav | cmix-lex | 109,671,639 | 1.01% | 5,050 EUR |
| 3 Sep 2024 | Kaido Orav & Byron Knoll | fx2-cmix | 110,793,128 | 1.59% | 7,950 EUR |
| 2 Feb 2024 | Kaido Orav | fx-cmix | 112,578,322 | 1.38% | 6,911 EUR |
| 16 Jul 2023 | Saurabh Kumar | fast cmix | 114,156,155 | 1.04% | 5,187 EUR |
| 31 May 2021 | Artemiy Margaritov | starlit | 115,352,938 | 1.1% | 9,000 EUR |
| 4 Jul 2019 | Alexander Rhatushnyak | phda9v1.8 | 116,673,681 | (pre-prize baseline) | n/a |

All three 2026 winners were awarded together on 30 Sep 2026 ([LTCB]: "awarded Sept. 30, 2026" for each). [M] "News" lists them as the ninth, tenth and eleventh winners. Total payout so far is 85,485 EUR [M], including the enwik8 era. Progress from the 2019 baseline to now is 13.9%.

**Breakdown of the current record** [LTCB #0969]: archive9 = 96,996,198 + compressor = 3,428,474 → **100,424,672**. The method replaces fx2-cmix's LSTM with a 6M-parameter transformer pre-trained offline on GPUs, quantized to 4-bit weights and 8-bit activations, and run on CPU. Measured on AMD: compression ≈ 37.2 h, decompression ≈ 36.5 h, about 9.5 to 9.7 GB RAM. Source: https://github.com/astOwOlfo/fx2-cmix-transformer-v1/ (linked from [M]).

Other breakdowns from [LTCB]:
- fx2-cmix: 110,351,665 (SFX) + 441,468 (compressor). Submitted 11 Aug 2024, accepted 8 Oct 2024.
- cmix-lex: 109,201,040 (archive9) + 470,599 (compressor).

### 5.2 Pending and other recent entries (NOT official records). From [LTCB], status as of its 14 Oct 2026 update
| Entry | Author | Date | archive9 | compressor | Total S | Status per [LTCB] |
|---|---|---|---|---|---|---|
| cmix-neif 1.0 | Marcus Wölk | 5 Oct 2026 | 91,913,343 | 4,000,689+19 | **95,914,051** | "Hutter prize submission (pending)" |
| cmix-lex-transformer lexth11c | Neel Patel | 27 Sep 2026 | 95,836,613 | 3,477,137 | 99,313,750 | "pending" |
| zmix 1.0 | James Byrne | 14 Sep 2026 | 96,096,261 | 3,216,163 | 99,312,424 | "pending ... still undergoing testing" |
| ex-girlfriend 1.0 | Ryan Landay | 14 Oct 2026 | 91,778,237 | 3,995,122 | 95,773,359 | "formatted entry (not eligible)" |
| makimono 1.0 | A. Sklyarenko | 13 Oct 2026 | 95,793,872 | 3,984,978 | 99,788,850 | "not eligible" |
| fx2-cmix-t-ft8 | J. Mejuto González | 10 Oct 2026 | 96,945,005 | 3,422,266 | 100,367,271 | "not eligible" |

Implications (my analysis, **UNVERIFIED** until [M] is updated):
- Submissions are processed in order received [R]. zmix (14 Sep) would come first: it is 1.11% below L and would set L ≈ 99,312,424. lexth11c (27 Sep) would then not be ≥ 1% better than zmix. cmix-neif (5 Oct) is 3.42% better than zmix. **If all of this is verified, L becomes ≈ 95,914,051, and a prize would then need S < ≈ 94,954,911.**
- "Not eligible" entries are clearly not candidates, but [LTCB] does not explain why for each one. Possible reasons include derivative or late timing, or not being formally submitted.

---

## 6. Reproducibility and publication requirements

- **OSI-approved open-source license** for documented source code, required "before the prize money will paid out" [R]. Preference order: Unlicense > MIT > Apache > MPL > GNU; all OSI licenses are acceptable. "Simply put a statement such as 'This Code is licensed under UNLICENSE http://unlicense.org' somewhere in your code." [F]#license. You may still copyright or patent, "as long as non-commercial use, and in particular use by other future contestants, is not restricted" [F]#source.
- **Write-up:** "A (link to a) document explaining the (algorithmic) ideas that led to or are incorporated into the algorithms and their inner working" [R], "Participation".
- **Public availability:** "The programs and/or data files must be available on the Internet for free download and testing." [R]
- **Waiting and comment period:** "There will be a waiting period of at least 30 days after submission to allow for public comment and verification." Comments go to the Hutter Prize Google Group (https://groups.google.com/group/Hutter-Prize) or to committee members [R].
- **Verification:** "in order to verify your claim we need to be able to run your executable on our machines" [R].
- **Self-superseding:** "If an author breaks his own record within 30 days, the older submission is regarded as withdrawn." [R]
- **Failure:** "If a submission fails to meet the criteria for the prize, the entrant will be informed, and the submission henceforth be ignored." [R]
- **Spirit clause:** "The prize will be paid if the solution reflects the spirit of the contest." [R]
- **Reuse:** winners' sources since 2020 are public and "can be used/adapted/improved with attribution" [F]#getsource. Most build on fast-cmix and starlit, and on fx2-cmix-transformer for transformer-based entries.

---

## 7. Our goal in context: S < 100,000,000

| Comparison | Bytes | % |
|---|---|---|
| L (official record) − 100,000,000 | 424,672 | **100M is 0.423% below L** |
| 100,000,000 − prize threshold (99,420,425) | 579,575 | we would need a further ~0.58% to claim the minimum prize |
| 100,000,000 vs pending cmix-neif (95,914,051) | +4,085,949 | 100M is 4.26% **larger** than the best pending entry |
| 100,000,000 vs pending zmix (99,312,424) | +687,576 | 100M is 0.69% larger |

Bottom line:
- Getting S under 100,000,000 within the rules would beat the **official** record, but by less than the 1% minimum. That means no money, and L does not change.
- A prize-eligible result against the official L needs **S ≤ 99,420,424**.
- If the pending cmix-neif entry is confirmed, the effective bar becomes about **94.95M**.
- The realistic baseline for us is to start from the open-source fx2-cmix-transformer, zmix or cmix-lex-transformer code. Recent entries reach about 99.3M (zmix, lexth11c) and about 95.9M (cmix-neif) under the same compute limits.

---

## 8. Rule-change history 2024–2026 (Wayback diffs)

- **[R], unchanged since 9 Oct 2024.** Diffing the Wayback snapshots from 2023-01-01 and 2024-11-28 against the current page shows only two changes. (a) The "2× can be reduced to 1× if comp9a = decomp9" clause was added. (b) The Geekbench link was changed to geekbench.com/legacy. The current page is byte-identical to the 2024-11-28 snapshot. **No rule changes in 2025 or 2026.**
- **[M], updated 30 Sep 2026** (vs the 2025-06-19 snapshot): record table and L updated (110,793,128 → 100,424,672), three new winners, payout total 29,945 → 85,485 EUR. The Task text and limits are unchanged.
- **[F], updated 30 Sep 2026** (vs the 2025-06-24 snapshot): new text endorsing offline pre-trained transformers ([F]#largenn, #getsource), progress statistics, and link fixes. No new restrictions.
- **Known stale text on the official pages** (not rule changes): the [R] header says "current record of about 116MB"; [R] says "~230 byte" per EUR; [F]#cpucore mentions "100 hours" and "500'000/T"; [F]#ongoing says "as of 2023".
- **UNVERIFIED:** I did not check the Google Group (https://groups.google.com/group/Hutter-Prize) for announcements that were not mirrored on the website. The zmix entry mentions an "entry ENV for automated judging" [LTCB #0960], which suggests the committee may now use a scripted test harness. Its format is not documented on the official pages.

---

## Verification note (2026-11-04, orchestrator spot-check)

A second agent re-fetched the sources independently:
- `prize.hutter1.net` **failed with an expired TLS certificate**. The record L = 100,424,672 was not re-confirmed directly. Use the Wayback Machine or a browser to re-check.
- `mattmahoney.net/dc/text.html` (Large Text Compression Benchmark) lists **cmix-neif 1.0, cmix-lex-transformer lexth11c and zmix 1.0** with enwik9 figures **below 100M**. The two fetches disagree on which column is which (compressed size vs. total) and on whether the rows are still pending. **UNVERIFIED.** Read the table by hand before relying on it.
- **Takeaway:** sub-100M total S is no longer a frontier claim. Transformer-augmented cmix entries already appear to be at or below it, so it's a good stepping-stone target but not a prize-winning one.
