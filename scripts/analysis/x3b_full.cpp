// X3b: forecast-combination diagnostics and causal final-stage recalibration sims on a per-byte cost log
// (patches/tools/per-byte-cost format, 40 B per coded byte), e.g. the C3 full-run dump build/exp/FULL/cost_full.bin.
//
//   x3b_full cost.bin [max_records] [threads] > report.txt
//
// Streams the file (never loads it whole). Per coded bit it has the coder's final probability (pbit) and the bit; per
// byte the alone-costs of the transformer, fxcm and PPMd and the match state. Everything used as a context for bit t is a
// function of earlier bytes' records, the current byte's match snapshot (taken before its first bit) and the current
// byte's already-coded bits, so every stage is decoder-reproducible.
//
// Part A (byte level): totals, oracle switching bound, burstiness (autocorrelation, cost by recent-cost regime),
//   expert-winner transition matrix, error correlation, and regime cells where an expert alone beats the final mix.
// Part B (bit level): for each context set, (1) a hindsight reliability table (cell x stretch bin: KL of the empirical
//   rate from the mean forecast = bits a perfect per-cell recalibration would save, overfit-corrected by 0.72 bit per
//   occupied bin), and (2) a causal online stage: logit-offset APM (piecewise-linear offset on stretch(p), SGD on
//   log loss, identity at start) per context cell. Reported delta is in bytes against the dump's own coded cost.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <omp.h>

#pragma pack(push, 1)
struct Rec {
  uint8_t byte, cm_len, cm_pred, fx_len, fx_pred, sm_len, sm_pred, flags;
  uint16_t pbit[8];
  float cost, cost_tf, cost_fx, cost_ppmd;
};
#pragma pack(pop)

static float g_st[65536];   // stretch of q/65536
static inline float squash(float x) { return 1.f / (1.f + std::exp(-x)); }

static const int NK = 25;             // APM knots at stretch -12..12 step 1
struct Stage {
  std::string name;
  uint32_t cells = 1;
  float lr = 0.02f, lrmin = 0.002f, tau = 256;
  std::vector<float> d;               // cells * NK logit offsets
  std::vector<uint32_t> cnt;          // per cell
  // hindsight reliability: cells * HB bins: n, sum p, sum bit
  static const int HB = 24;
  std::vector<double> hn, hp, hb, hc; // hc: cost sum per cell (bits)
  double delta_seg[20] = {0}, base_seg[20] = {0}, delta_bpos[8] = {0};
  bool causal = true;
  void init() {
    d.assign((size_t)cells * NK, 0.f); cnt.assign(cells, 0);
    hn.assign((size_t)cells * HB, 0); hp = hn; hb = hn; hc.assign(cells, 0);
  }
};

static inline int qcost6(float c) { return c < 0.02f ? 0 : c < 0.1f ? 1 : c < 0.5f ? 2 : c < 1.5f ? 3 : c < 4.f ? 4 : 5; }
static inline int qfast8(float c) { return c < 0.1f ? 0 : c < 0.3f ? 1 : c < 0.7f ? 2 : c < 1.3f ? 3 : c < 2.f ? 4 : c < 3.f ? 5 : c < 4.5f ? 6 : 7; }
static inline int qslow6(float c) { return c < 0.5f ? 0 : c < 1.f ? 1 : c < 1.5f ? 2 : c < 2.f ? 3 : c < 3.f ? 4 : 5; }
static inline int qskill5(float s) { return s < -0.2f ? 0 : s < 0.05f ? 1 : s < 0.3f ? 2 : s < 1.f ? 3 : 4; }
static inline int qskill3(float s) { return s < 0.3f ? 0 : s < 1.5f ? 1 : 2; }
static inline int bclass(int b) {
  if (b >= 0xC0) return 1;
  if (b >= 0x80) return 0;
  if (b >= '0' && b <= '9') return 2;
  if (b >= 'a' && b <= 'z') return 3;
  if (b >= 'A' && b <= 'Z') return 4;
  if (b == ' ' || b == '\n') return 5;
  if (b < 0x20 || b == 0x40) return 6;
  return 7;
}
static inline int mstate(int len, int pred, int c0, int bpos) {  // 0 none, 1 mismatch, 2 expects 0, 3 expects 1
  if (!len) return 0;
  if (((pred | 0x100) >> (8 - bpos)) != c0) return 1;
  return 2 + ((pred >> (7 - bpos)) & 1);
}

int main(int argc, char** argv) {
  if (argc < 2) return 1;
  long maxrec = argc > 2 ? atol(argv[2]) : 0;
  int threads = argc > 3 ? atoi(argv[3]) : 4;
  omp_set_num_threads(threads);
  FILE* f = fopen(argv[1], "rb");
  char hdr[16];
  if (!f || fread(hdr, 1, 16, f) != 16 || memcmp(hdr, "FX2COST", 7)) { fprintf(stderr, "bad file\n"); return 1; }
  fseek(f, 0, SEEK_END); long total = (ftell(f) - 16) / 40; fseek(f, 16, SEEK_SET);
  if (maxrec && maxrec < total) total = maxrec;
  for (int q = 0; q < 65536; ++q) { double p = (q < 1 ? 1 : q) / 65536.0; g_st[q] = (float)std::log(p / (1 - p)); }

  enum { C_NONE, C_C0, C_INBYTE, C_EMA, C_WIN, C_SKILL, C_HAZ, C_MATCH, C_CLASS, C_PREV2, C_POS, C_C0IN, C_COMBO, C_MATCHIN, C_SKILLIN, NCTX };
  const char* cname[NCTX] = {"none", "c0(partial byte)", "bpos x cost-so-far-in-byte", "bpos x EMA cost fast x slow",
    "bpos x winner(t-1) x winner(t-2) x mix-beat-all", "bpos x skill(tf) x skill(fx) x skill(ppmd)", "bpos x bytes-since-surprise",
    "bpos x match state (cm,fx: len x expect)", "bpos x class(b-1) x class(b-2)", "bpos x cost(b-1) x cost(b-2)", "bpos x file-position 20ths",
    "c0 x cost-so-far-in-byte", "bpos x match-expect(cm,fx,sm) x winner x cost-so-far", "c0 x match-expect(cm,fx) x cost-so-far(3)",
    "bpos x skill(tf) x skill(fx) x cost-so-far"};
  const uint32_t ccells[NCTX] = {1, 256, 8 * 6, 8 * 8 * 6, 8 * 18, 8 * 75, 8 * 12, 8 * 144, 8 * 64, 8 * 36, 8 * 20, 256 * 6, 8 * 64 * 3 * 6, 256 * 16 * 3, 8 * 25 * 6};
  // stage list: every context at the default rate, plus rate variants of a few
  std::vector<Stage> S; std::vector<int> sctx;
  auto add = [&](int c, float lr, float lrmin, float tau) {
    Stage s; char b[160]; snprintf(b, sizeof b, "%s [lr %g/%g tau %g]", cname[c], lr, lrmin, tau);
    s.name = b; s.cells = ccells[c]; s.lr = lr; s.lrmin = lrmin; s.tau = tau; s.init(); S.push_back(std::move(s)); sctx.push_back(c);
  };
  // three rate settings per context (the report shows all three; none is tuned on the scored data beyond that)
  for (int c = 0; c < NCTX; ++c) add(c, 0.002f, 0.0001f, 1024);
  for (int c = 0; c < NCTX; ++c) { add(c, 0.0005f, 0.00002f, 4096); add(c, 0.005f, 0.0004f, 512); }
  const int NS = S.size();

  const size_t CH = 1 << 19;
  std::vector<Rec> buf(CH);
  std::vector<uint16_t> Q(CH * 8); std::vector<uint8_t> B(CH * 8), SEG(CH * 8);
  std::vector<std::vector<uint32_t>> CX(NCTX, std::vector<uint32_t>(CH * 8));

  // byte-level state
  float ef = 1.5f, es = 1.5f, sk_tf = 0.2f, sk_fx = 1.f, sk_pp = 2.f, pc1 = 1, pc2 = 1;
  int w1 = 0, w2 = 0, beat = 0, haz = 0, b1 = 0, b2 = 0;
  // part A accumulators
  double tc = 0, ttf = 0, tfx = 0, tpp = 0, tmin3 = 0, tmin4 = 0, tover = 0; long nover = 0;
  double trans[3][3] = {{0}}; double wcnt[3] = {0};
  const int LAGS[8] = {1, 2, 4, 8, 16, 32, 64, 256}; double ac[8] = {0}; double sc = 0, sc2 = 0;
  std::vector<float> ring(512, 0); long ri = 0;
  double emaN[8] = {0}, emaC[8] = {0}, emaC2[8] = {0};
  double hazN[12] = {0}, hazC[12] = {0};
  // regime cells: w1(3) x w2(3) x skill tf(5) x skill fx(5) x skill pp(3): sums of the four costs
  const int RC = 3 * 3 * 75; std::vector<double> rN(RC, 0), rC(RC, 0), rT(RC, 0), rF(RC, 0), rP(RC, 0);
  double cov[4][4] = {{0}}, mean4[4] = {0};
  double hist_cost[8] = {0}, hist_n[8] = {0};  // cost distribution: <0.01,<0.1,<0.5,<1,<2,<4,<8,>=8
  double clsN[8] = {0}, clsC[8] = {0}, clsT[8] = {0}, clsF[8] = {0}, clsP[8] = {0};

  long done = 0;
  while (done < total) {
    size_t want = (size_t)std::min<long>(CH, total - done);
    size_t got = fread(buf.data(), 40, want, f);
    if (!got) break;
    for (size_t r = 0; r < got; ++r) {
      const Rec& x = buf[r];
      const long gi = done + r; const int seg = (int)(gi * 20 / total);
      // contexts fixed for the byte
      const int kw = (w1 * 3 + w2) * 2 + beat;
      const int ksk = (qskill5(sk_tf) * 5 + qskill5(sk_fx)) * 3 + qskill3(sk_pp);
      const int kema = qfast8(ef) * 6 + qslow6(es);
      int hb = 0; { int h = haz; while (h > 0 && hb < 11) { h >>= 1; ++hb; } }
      const int kcl = bclass(b1) * 8 + bclass(b2);
      const int kp2 = qcost6(pc1) * 6 + qcost6(pc2);
      const int cml = x.cm_len == 0 ? 0 : x.cm_len <= 32 ? 1 : x.cm_len <= 128 ? 2 : 3;
      const int fxl = x.fx_len == 0 ? 0 : x.fx_len <= 8 ? 1 : x.fx_len <= 32 ? 2 : 3;
      float cum = 0; int c0 = 1;
      for (int i = 0; i < 8; ++i) {
        const size_t t = r * 8 + i;
        const int bit = (x.byte >> (7 - i)) & 1; const unsigned q = x.pbit[i];
        Q[t] = q; B[t] = bit; SEG[t] = seg;
        const int ms_cm = mstate(x.cm_len, x.cm_pred, c0, i), ms_fx = mstate(x.fx_len, x.fx_pred, c0, i), ms_sm = mstate(x.sm_len, x.sm_pred, c0, i);
        const int qc = qcost6(cum), qc3 = cum < 0.1f ? 0 : cum < 1.5f ? 1 : 2;
        CX[C_NONE][t] = 0; CX[C_C0][t] = c0;
        CX[C_INBYTE][t] = i * 6 + qc;
        CX[C_EMA][t] = i * 48 + kema;
        CX[C_WIN][t] = i * 18 + kw;
        CX[C_SKILL][t] = i * 75 + ksk;
        CX[C_HAZ][t] = i * 12 + hb;
        CX[C_MATCH][t] = i * 144 + ((cml * 3 + (ms_cm ? ms_cm - 1 : 0)) * 12 + fxl * 3 + (ms_fx ? ms_fx - 1 : 0));
        CX[C_CLASS][t] = i * 64 + kcl;
        CX[C_PREV2][t] = i * 36 + kp2;
        CX[C_POS][t] = i * 20 + seg;
        CX[C_C0IN][t] = c0 * 6 + qc;
        CX[C_COMBO][t] = ((i * 64 + ms_cm * 16 + ms_fx * 4 + ms_sm) * 3 + w1) * 6 + qc;
        CX[C_MATCHIN][t] = (c0 * 16 + ms_cm * 4 + ms_fx) * 3 + qc3;
        CX[C_SKILLIN][t] = (i * 25 + qskill5(sk_tf) * 5 + qskill5(sk_fx)) * 6 + qc;
        const double pq = (bit ? q : 65536 - q) / 65536.0;
        cum += (float)-std::log2(pq);
        c0 = c0 * 2 + bit;
      }
      // ---- part A stats (use state before update) ----
      const float c = x.cost, t = x.cost_tf, fx = x.cost_fx, pp = x.cost_ppmd;
      tc += c; ttf += t; tfx += fx; tpp += pp;
      float m3 = std::min(t, std::min(fx, pp)); tmin3 += m3; tmin4 += std::min(m3, c);
      if (x.flags & 2) { tover += c; ++nover; }
      const int w = (t <= fx && t <= pp) ? 0 : (fx <= pp ? 1 : 2);
      trans[w1][w] += 1; wcnt[w] += 1;
      sc += c; sc2 += (double)c * c;
      for (int l = 0; l < 8; ++l) if (gi >= LAGS[l]) ac[l] += (double)c * ring[(ri - LAGS[l]) & 511];
      ring[ri & 511] = c; ++ri;
      { int e = qfast8(ef); emaN[e] += 1; emaC[e] += c; emaC2[e] += (double)c * c; }
      hazN[hb] += 1; hazC[hb] += c;
      { int k = (w1 * 3 + w2) * 75 + ksk; rN[k] += 1; rC[k] += c; rT[k] += t; rF[k] += fx; rP[k] += pp; }
      { double v[4] = {c, t, fx, pp}; for (int a = 0; a < 4; ++a) { mean4[a] += v[a]; for (int b = 0; b < 4; ++b) cov[a][b] += v[a] * v[b]; } }
      { int h = c < 0.01f ? 0 : c < 0.1f ? 1 : c < 0.5f ? 2 : c < 1 ? 3 : c < 2 ? 4 : c < 4 ? 5 : c < 8 ? 6 : 7; hist_n[h] += 1; hist_cost[h] += c; }
      { int k = bclass(x.byte); clsN[k] += 1; clsC[k] += c; clsT[k] += t; clsF[k] += fx; clsP[k] += pp; }
      // ---- state update ----
      ef += (c - ef) * 0.25f; es += (c - es) * (1.f / 32);
      sk_tf += (t - c - sk_tf) * 0.125f; sk_fx += (fx - c - sk_fx) * 0.125f; sk_pp += (pp - c - sk_pp) * 0.125f;
      w2 = w1; w1 = w; beat = c <= m3;
      haz = c >= 4.f ? 0 : haz + 1;
      pc2 = pc1; pc1 = c; b2 = b1; b1 = x.byte;
    }
    const size_t nb = got * 8;
    #pragma omp parallel for schedule(dynamic, 1)
    for (int s = 0; s < NS; ++s) {
      Stage& st = S[s]; const uint32_t* cx = CX[sctx[s]].data();
      const bool hind = s < NCTX;
      for (size_t t = 0; t < nb; ++t) {
        const unsigned q = Q[t]; if (q <= 1 || q >= 65535) continue;   // transformer override bits pass through
        const int bit = B[t]; const float x = g_st[q]; const uint32_t c = cx[t];
        float u = (x + 12.f); if (u < 0) u = 0; if (u > 23.999f) u = 23.999f;
        const int k = (int)u; const float fr = u - k;
        float* d = &st.d[(size_t)c * NK + k];
        const float x2 = x + d[0] * (1 - fr) + d[1] * fr;
        float p2 = squash(x2);
        unsigned q2 = (unsigned)(1.0f + 65534.0f * p2); if (q2 < 1) q2 = 1; if (q2 > 65535) q2 = 65535;
        const double cb = -std::log2((bit ? q : 65536 - q) / 65536.0), cn = -std::log2((bit ? q2 : 65536 - q2) / 65536.0);
        st.delta_seg[SEG[t]] += cn - cb; st.base_seg[SEG[t]] += cb; st.delta_bpos[t & 7] += cn - cb;
        const uint32_t n = st.cnt[c]++;
        const float l = st.lrmin + (st.lr - st.lrmin) * st.tau / (st.tau + n);
        const float g = l * (p2 - bit) * 8.f;   // x8: offsets move in stretch units
        d[0] -= g * (1 - fr); d[1] -= g * fr;
        if (hind) {
          int hbk = (int)((x + 12.f)); if (hbk < 0) hbk = 0; if (hbk > 23) hbk = 23;
          const size_t h = (size_t)c * Stage::HB + hbk;
          st.hn[h] += 1; st.hp[h] += q / 65536.0; st.hb[h] += bit; st.hc[c] += cb;
        }
      }
    }
    done += got;
    if ((done / CH) % 64 == 0) fprintf(stderr, "%ld / %ld\n", done, total);
  }
  const double N = done;
  printf("# x3b_full: %s, %ld coded bytes\n\n## A. byte level\n", argv[1], done);
  printf("final %.0f B (%.4f bits/byte); tf alone %.0f B; fxcm alone %.0f B; ppmd alone %.0f B\n", tc / 8, tc / N, ttf / 8, tfx / 8, tpp / 8);
  printf("oracle per-byte min of 3 experts %.0f B; min of 3 experts and final %.0f B (final - oracle4 = %.0f B)\n", tmin3 / 8, tmin4 / 8, (tc - tmin4) / 8);
  printf("override bytes %ld, cost %.0f B\n", nover, tover / 8);
  printf("cost distribution (bucket: share of bytes, share of bits):");
  const char* hn[8] = {"<0.01", "<0.1", "<0.5", "<1", "<2", "<4", "<8", ">=8"};
  for (int h = 0; h < 8; ++h) printf(" %s: %.1f%% %.1f%%;", hn[h], 100 * hist_n[h] / N, 100 * hist_cost[h] / tc);
  const double mu = sc / N, var = sc2 / N - mu * mu;
  printf("\ncost mean %.4f var %.4f; autocorrelation:", mu, var);
  for (int l = 0; l < 8; ++l) printf(" lag%d %.3f", LAGS[l], (ac[l] / N - mu * mu) / var);
  printf("\nmean next-byte cost by fast EMA bucket (n share, mean, sd):");
  for (int e = 0; e < 8; ++e) if (emaN[e] > 0) { double m = emaC[e] / emaN[e]; printf(" [%d] %.1f%% %.3f %.3f;", e, 100 * emaN[e] / N, m, std::sqrt(emaC2[e] / emaN[e] - m * m)); }
  printf("\nmean cost by log2(bytes since a >=4-bit byte):");
  for (int h = 0; h < 12; ++h) if (hazN[h] > 0) printf(" [%d] %.1f%% %.3f;", h, 100 * hazN[h] / N, hazC[h] / hazN[h]);
  printf("\nwinner shares tf %.3f fx %.3f ppmd %.3f; transition P(w_t | w_t-1) rows tf,fx,ppmd:\n", wcnt[0] / N, wcnt[1] / N, wcnt[2] / N);
  for (int a = 0; a < 3; ++a) { double s = trans[a][0] + trans[a][1] + trans[a][2]; printf("  %.3f %.3f %.3f\n", trans[a][0] / s, trans[a][1] / s, trans[a][2] / s); }
  printf("correlation of byte costs (final, tf, fx, ppmd):\n");
  for (int a = 0; a < 4; ++a) { printf(" "); for (int b = 0; b < 4; ++b) {
    double ca = cov[a][b] / N - mean4[a] / N * mean4[b] / N, va = cov[a][a] / N - mean4[a] / N * mean4[a] / N, vb = cov[b][b] / N - mean4[b] / N * mean4[b] / N;
    printf(" %.3f", ca / std::sqrt(va * vb)); } printf("\n"); }
  printf("by class of the coded byte (0 0x80-BF, 1 0xC0-FF, 2 digit, 3 lower, 4 upper, 5 space/nl, 6 ctrl/@, 7 punct): share bytes, share bits, bits/byte final tf fx ppmd\n");
  for (int k = 0; k < 8; ++k) if (clsN[k] > 0) printf("  [%d] %.1f%% %.1f%% %.3f %.3f %.3f %.3f\n", k, 100 * clsN[k] / N, 100 * clsC[k] / tc, clsC[k] / clsN[k], clsT[k] / clsN[k], clsF[k] / clsN[k], clsP[k] / clsN[k]);
  { // regime cells where an expert alone beats the final mix
    double gt = 0, gf = 0, gp = 0, nt = 0, nf = 0, np_ = 0;
    for (int k = 0; k < RC; ++k) { if (rN[k] < 1000) continue;
      if (rT[k] < rC[k]) { gt += rC[k] - rT[k]; nt += rN[k]; } if (rF[k] < rC[k]) { gf += rC[k] - rF[k]; nf += rN[k]; } if (rP[k] < rC[k]) { gp += rC[k] - rP[k]; np_ += rN[k]; } }
    printf("regime cells (winner t-1 x winner t-2 x skill buckets, %d cells) where an expert ALONE beats the final mix on average:\n", RC);
    printf("  tf: %.0f B over %.2f%% of bytes; fxcm: %.0f B over %.2f%%; ppmd: %.0f B over %.2f%%\n", gt / 8, 100 * nt / N, gf / 8, 100 * nf / N, gp / 8, 100 * np_ / N);
  }
  printf("\n## B. bit level: hindsight reliability and causal online recalibration, per context set\n");
  printf("| context | cells | hindsight KL (B) | overfit-corrected (B) | causal online delta (B) | %% of coded |\n|---|---:|---:|---:|---:|---:|\n");
  for (int s = 0; s < NS; ++s) {
    Stage& st = S[s]; double dl = 0, bs = 0; for (int g = 0; g < 20; ++g) { dl += st.delta_seg[g]; bs += st.base_seg[g]; }
    double kl = 0, occ = 0;
    if (s < NCTX) for (size_t h = 0; h < st.hn.size(); ++h) { double n = st.hn[h]; if (n < 1) continue; occ += 1;
      double pb = st.hp[h] / n, bb = st.hb[h] / n; double k = 0;
      if (bb > 0) k += bb * std::log2(bb / pb); if (bb < 1) k += (1 - bb) * std::log2((1 - bb) / (1 - pb)); kl += n * k; }
    if (s < NCTX) printf("| %s | %u | %.0f | %.0f | %+.0f | %+.4f%% |\n", st.name.c_str(), st.cells, kl / 8, (kl - 0.72 * occ) / 8, dl / 8, 100 * dl / bs);
    else printf("| %s | %u | | | %+.0f | %+.4f%% |\n", st.name.c_str(), st.cells, dl / 8, 100 * dl / bs);
  }
  printf("\ncausal delta by 20ths of the file (B):\n");
  for (int s = 0; s < NS; ++s) { printf("  %-70s", S[s].name.c_str()); for (int g = 0; g < 20; ++g) printf(" %+.0f", S[s].delta_seg[g] / 8); printf("\n"); }
  printf("\ncausal delta by bit position, MSB first (B):\n");
  for (int s = 0; s < NS; ++s) { printf("  %-70s", S[s].name.c_str()); for (int g = 0; g < 8; ++g) printf(" %+.0f", S[s].delta_bpos[g] / 8); printf("\n"); }
  printf("\n## top hindsight cells per context (cell, bits n, cost B, KL B, mean p, mean bit in the worst bin)\n");
  for (int s = 0; s < NCTX; ++s) { Stage& st = S[s]; if (st.cells < 2) {
      printf("%s: reliability diagram (stretch bin: n, mean p, mean bit, KL B)\n", st.name.c_str());
      for (int h = 0; h < Stage::HB; ++h) { double n = st.hn[h]; if (n < 1) continue; double pb = st.hp[h] / n, bb = st.hb[h] / n, k = 0;
        if (bb > 0) k += bb * std::log2(bb / pb); if (bb < 1) k += (1 - bb) * std::log2((1 - bb) / (1 - pb));
        printf("  [%+d,%+d): %.0f %.6f %.6f %.0f\n", h - 12, h - 11, n, pb, bb, n * k / 8); }
      continue; }
    std::vector<std::pair<double, uint32_t>> v;
    for (uint32_t c = 0; c < st.cells; ++c) { double kl = 0;
      for (int h = 0; h < Stage::HB; ++h) { double n = st.hn[(size_t)c * Stage::HB + h]; if (n < 1) continue; double pb = st.hp[(size_t)c * Stage::HB + h] / n, bb = st.hb[(size_t)c * Stage::HB + h] / n, k = 0;
        if (bb > 0) k += bb * std::log2(bb / pb); if (bb < 1) k += (1 - bb) * std::log2((1 - bb) / (1 - pb)); kl += n * k - 0.72; }
      v.push_back({kl, c}); }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    printf("%s:\n", st.name.c_str());
    for (int i = 0; i < 6 && i < (int)v.size(); ++i) { uint32_t c = v[i].second; double n = 0, wk = -1; int wh = 0;
      for (int h = 0; h < Stage::HB; ++h) { double nn = st.hn[(size_t)c * Stage::HB + h]; n += nn; if (nn < 1) continue; double pb = st.hp[(size_t)c * Stage::HB + h] / nn, bb = st.hb[(size_t)c * Stage::HB + h] / nn, k = 0;
        if (bb > 0) k += bb * std::log2(bb / pb); if (bb < 1) k += (1 - bb) * std::log2((1 - bb) / (1 - pb)); if (nn * k > wk) { wk = nn * k; wh = h; } }
      double nn = st.hn[(size_t)c * Stage::HB + wh];
      printf("  cell %u: n %.0f cost %.0f B KL %.0f B; worst bin [%+d,%+d) n %.0f p %.5f bit %.5f\n", c, n, st.hc[c] / 8, v[i].first / 8, wh - 12, wh - 11, nn, st.hp[(size_t)c * Stage::HB + wh] / nn, st.hb[(size_t)c * Stage::HB + wh] / nn); }
  }
  return 0;
}
