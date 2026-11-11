// X3b: causal sims of forecast-combination stages ON TOP OF C4's final stage (G1 with the BITLSTM-delta gate feature),
// over a G1 final-stage dump (patches/exp/G1-gln/dump.sh; per bit: the 26 layer-1 inputs, raw final logit, p after SSE,
// p after X2, coded p, bit, override, partial byte, last 2 bytes, E1 buckets).
//
//   x3b_sim dump.bin [record_costlog.bin|-] [max_bits] > report
//
// The base is an exact re-run of g1_gln.h on the dump with C4's z[3] (reproduces the C4 README's sim numbers: 10M -842.6 B
// vs C3). T1 is not in the dump (it acts on interlanguage titles only). Each candidate stage reads G1's output logit and
// features that the decoder also has (earlier bits' probabilities and outcomes, the experts' current predictions) and
// learns online from the first bit: no pretraining, no second pass. The optional record cost log (same coded stream,
// one record per byte) supplies the match-model snapshot taken before a byte's first bit.
// Deltas are bytes against C4's coded cost on the same bits.
#define private public
#include "../../patches/exp/G1-gln/g1_gln.h"
#undef private
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#pragma pack(push, 1)
struct Rec { uint8_t byte, cm_len, cm_pred, fx_len, fx_pred, sm_len, sm_pred, flags; uint16_t pbit[8]; float cost, cost_tf, cost_fx, cost_ppmd; };
#pragma pack(pop)

static inline float sqsh(float x) { return 1.f / (1.f + std::exp(-x)); }
static inline float clip12(float x) { return x > 12 ? 12 : x < -12 ? -12 : x; }
static inline unsigned quant(float p) { unsigned q = (unsigned)(1.0f + 65534.0f * p); return q < 1 ? 1 : q > 65535 ? 65535 : q; }
static inline double bitcost(unsigned q, int bit) { return -std::log2((bit ? q : 65536 - q) / 65536.0); }

// Per-bit features shared by all stages (all decoder-side).
struct Feat {
  float stg;                 // C4 output logit (G1), unquantized
  float sc3;                 // stretch of C3's coded p (G1's input)
  float tf, fx, e2, m1, mmean, mmed, spsse, spfin;
  float mix[23];
  float gn[8];                // G1's neuron logits
  int votes;                 // layer-0 mixers above stg (0..23)
  int bpos, c0, b1, b2;
  float cum;                 // C4 cost of this byte's earlier bits
  float ef, es;              // EMA of C4 byte cost (1/4, 1/32)
  float sk_tf, sk_fx, sk_e2; // EMA(1/8) of (expert byte cost - C4 byte cost)
  int w1, w2, haz;           // previous winners among tf/fx (0/1), bytes since a >=4-bit byte
  float pc1, pc2;
  int ms_cm, ms_fx, ms_sm, cml, fxl;   // match state (0 none/absent)
  float fsw;                 // fixed-share posterior weight of the G1 expert
  float sfs;                 // stretch of the fixed-share arithmetic pool
};

struct Stage {
  std::string name; double tot = 0; double seg[10] = {0};
  virtual float predict(const Feat& f) = 0;   // returns logit
  virtual void update(int bit) = 0;
  virtual ~Stage() {}
};

static inline int qcost6(float c) { return c < 0.02f ? 0 : c < 0.1f ? 1 : c < 0.5f ? 2 : c < 1.5f ? 3 : c < 4.f ? 4 : 5; }
static inline int qfast8(float c) { return c < 0.1f ? 0 : c < 0.3f ? 1 : c < 0.7f ? 2 : c < 1.3f ? 3 : c < 2.f ? 4 : c < 3.f ? 5 : c < 4.5f ? 6 : 7; }
static inline int qslow6(float c) { return c < 0.5f ? 0 : c < 1.f ? 1 : c < 1.5f ? 2 : c < 2.f ? 3 : c < 3.f ? 4 : 5; }
static inline int qskill5(float s) { return s < -0.2f ? 0 : s < 0.05f ? 1 : s < 0.3f ? 2 : s < 1.f ? 3 : 4; }
static inline int qd9(float d) { return d < -4 ? 0 : d < -1.5f ? 1 : d < -0.5f ? 2 : d < -0.1f ? 3 : d < 0.1f ? 4 : d < 0.5f ? 5 : d < 1.5f ? 6 : d < 4 ? 7 : 8; }
static inline int bclass(int b) {
  if (b >= 0xC0) return 1; if (b >= 0x80) return 0; if (b >= '0' && b <= '9') return 2; if (b >= 'a' && b <= 'z') return 3;
  if (b >= 'A' && b <= 'Z') return 4; if (b == ' ' || b == '\n') return 5; if (b < 0x20 || b == 0x40) return 6; return 7;
}
static inline int mstate(int len, int pred, int c0, int bpos) { if (!len) return 0; if (((pred | 0x100) >> (8 - bpos)) != c0) return 1; return 2 + ((pred >> (7 - bpos)) & 1); }

typedef std::function<uint32_t(const Feat&)> CtxFn;

// Logit-offset APM: piecewise-linear offset on the input logit per context cell, SGD on log loss, identity at start.
struct LApm : Stage {
  static const int NK = 25; CtxFn cx; std::vector<float> d; std::vector<uint32_t> cnt; float lr, lrmin, tau;
  float* dd; float fr, p2; uint32_t c;
  LApm(const char* n, uint32_t cells, CtxFn fn, float lr_ = 0.002f, float lrmin_ = 0.0001f, float tau_ = 1024) : cx(fn), d((size_t)cells * NK, 0.f), cnt(cells, 0), lr(lr_), lrmin(lrmin_), tau(tau_) {
    char b[200]; snprintf(b, sizeof b, "LAPM %s (%u cells, lr %g/%g)", n, cells, lr, lrmin); name = b; }
  float predict(const Feat& f) override {
    c = cx(f); float u = f.stg + 12.f; if (u < 0) u = 0; if (u > 23.999f) u = 23.999f; int k = (int)u; fr = u - k;
    dd = &d[(size_t)c * NK + k]; float x2 = f.stg + dd[0] * (1 - fr) + dd[1] * fr; p2 = sqsh(x2); return x2; }
  void update(int bit) override { uint32_t n = cnt[c]++; float l = lrmin + (lr - lrmin) * tau / (tau + n); float g = l * (p2 - bit) * 8.f; dd[0] -= g * (1 - fr); dd[1] -= g * fr; }
};

// Small logistic mixer over K inputs with context-selected weight sets, identity on input 0 at start.
struct MixK : Stage {
  int K; CtxFn cx; std::function<void(const Feat&, float*)> in; std::vector<float> w; float lr; float x[48]; float p; float* ww;
  MixK(const char* n, int k, uint32_t cells, CtxFn fn, std::function<void(const Feat&, float*)> inf, float lr_) : K(k), cx(fn), in(inf), w((size_t)cells * k, 0.f), lr(lr_) {
    for (uint32_t c = 0; c < cells; ++c) w[(size_t)c * k] = 1.f; char b[200]; snprintf(b, sizeof b, "MIX %s (%d inputs, %u sets, lr %g)", n, k, cells, lr); name = b; }
  float predict(const Feat& f) override { in(f, x); ww = &w[(size_t)cx(f) * K]; float s = 0; for (int i = 0; i < K; ++i) s += ww[i] * x[i]; s = clip12(s); p = sqsh(s); return s; }
  void update(int bit) override { float g = lr * (p - bit); for (int i = 0; i < K; ++i) ww[i] -= g * x[i]; }
};

// Covariance-aware combiner: logistic regression by an extended Kalman filter (assumed-density / online Newton),
// full K x K posterior covariance per weight set, random-walk process noise q. Identity on input 0 at start.
struct Ekf : Stage {
  int K; CtxFn cx; std::function<void(const Feat&, float*)> in; std::vector<double> w, P; double q; float x[48]; double p; double* ww; double* PP;
  Ekf(const char* n, int k, uint32_t cells, CtxFn fn, std::function<void(const Feat&, float*)> inf, double p0, double q_) : K(k), cx(fn), in(inf), w((size_t)cells * k, 0.), P((size_t)cells * k * k, 0.), q(q_) {
    for (uint32_t c = 0; c < cells; ++c) { w[(size_t)c * k] = 1.; for (int i = 0; i < k; ++i) P[(size_t)c * k * k + i * k + i] = p0; }
    char b[200]; snprintf(b, sizeof b, "EKF %s (%d inputs, %u sets, P0 %g, q %g)", n, k, cells, p0, q_); name = b; }
  float predict(const Feat& f) override { in(f, x); uint32_t c = cx(f); ww = &w[(size_t)c * K]; PP = &P[(size_t)c * K * K]; double s = 0; for (int i = 0; i < K; ++i) s += ww[i] * x[i];
    s = s > 12 ? 12 : s < -12 ? -12 : s; p = 1 / (1 + std::exp(-s)); return (float)s; }
  void update(int bit) override {
    double Px[48]; double s = 0; for (int i = 0; i < K; ++i) { double a = 0; for (int j = 0; j < K; ++j) a += PP[i * K + j] * x[j]; Px[i] = a; s += a * x[i]; }
    double h = p * (1 - p); double den = 1 + h * s; double e = (bit - p) / den;
    for (int i = 0; i < K; ++i) ww[i] += Px[i] * e;
    double k = h / den; for (int i = 0; i < K; ++i) for (int j = 0; j < K; ++j) PP[i * K + j] -= k * Px[i] * Px[j];
    for (int i = 0; i < K; ++i) PP[i * K + i] += q;
  }
};

// A second GLN (the G1 class) with its own base inputs and side information.
struct Gln2 : Stage {
  g1::Gln g; std::function<void(const Feat&, float*, float*)> in; float X[8], Z[8];
  Gln2(const char* n, g1::Config c, std::function<void(const Feat&, float*, float*)> inf) : g(c), in(inf) {
    char b[200]; snprintf(b, sizeof b, "GLN2 %s (n %d k %d lr %g/%g seed %d)", n, c.n, c.k, c.lr, c.lrmin, c.seed); name = b; }
  float predict(const Feat& f) override { in(f, X, Z); return g.Predict(X, Z); }
  void update(int bit) override { g.Update(bit); }
};

// Chain: stage B applied to stage A's output logit (B sees f.stg replaced).
struct Chain : Stage {
  std::unique_ptr<Stage> a, b; Feat f2;
  Chain(Stage* a_, Stage* b_) : a(a_), b(b_) { name = "CHAIN " + a->name + " -> " + b->name; }
  float predict(const Feat& f) override { f2 = f; f2.stg = clip12(a->predict(f)); return b->predict(f2); }
  void update(int bit) override { a->update(bit); b->update(bit); }
};

int main(int argc, char** argv) {
  if (argc < 2) return 1;
  FILE* f = fopen(argv[1], "rb"); char hdr[8]; uint32_t n;
  if (!f || fread(hdr, 1, 8, f) != 8 || fread(&n, 4, 1, f) != 1 || n != 26) { fprintf(stderr, "bad dump\n"); return 1; }
  FILE* fc = (argc > 2 && strcmp(argv[2], "-")) ? fopen(argv[2], "rb") : nullptr;
  if (fc) fseek(fc, 16, SEEK_SET);
  long maxbits = argc > 3 ? atol(argv[3]) : 0;
  fseek(f, 0, SEEK_END); long total = (ftell(f) - 12) / 128; fseek(f, 12, SEEK_SET); if (maxbits && maxbits < total) total = maxbits;
  const int R = 128, nmix = 23;

  std::vector<std::unique_ptr<Stage>> S;
  auto bp = [](const Feat& f) -> uint32_t { return f.bpos; };
  // ---- K1: conditional recalibration (reliability by regime) ----
  S.emplace_back(new LApm("none", 1, [](const Feat&) -> uint32_t { return 0; }));
  S.emplace_back(new LApm("c0", 256, [](const Feat& f) -> uint32_t { return f.c0; }));
  S.emplace_back(new LApm("bpos x cost-so-far", 48, [](const Feat& f) -> uint32_t { return f.bpos * 6 + qcost6(f.cum); }));
  S.emplace_back(new LApm("bpos x EMA fast x slow", 384, [](const Feat& f) -> uint32_t { return f.bpos * 48 + qfast8(f.ef) * 6 + qslow6(f.es); }));
  S.emplace_back(new LApm("bpos x winner2 ", 32, [](const Feat& f) -> uint32_t { return f.bpos * 4 + f.w1 * 2 + f.w2; }));
  S.emplace_back(new LApm("bpos x skill tf x fx", 200, [](const Feat& f) -> uint32_t { return f.bpos * 25 + qskill5(f.sk_tf) * 5 + qskill5(f.sk_fx); }));
  S.emplace_back(new LApm("bpos x since-surprise", 96, [](const Feat& f) -> uint32_t { int hb = 0, h = f.haz; while (h > 0 && hb < 11) { h >>= 1; ++hb; } return f.bpos * 12 + hb; }));
  S.emplace_back(new LApm("bpos x class2", 512, [](const Feat& f) -> uint32_t { return f.bpos * 64 + bclass(f.b1) * 8 + bclass(f.b2); }));
  S.emplace_back(new LApm("bpos x cost(b-1) x cost(b-2)", 288, [](const Feat& f) -> uint32_t { return f.bpos * 36 + qcost6(f.pc1) * 6 + qcost6(f.pc2); }));
  S.emplace_back(new LApm("bpos x match state", 1152, [](const Feat& f) -> uint32_t { return f.bpos * 144 + ((f.cml * 3 + (f.ms_cm ? f.ms_cm - 1 : 0)) * 12 + f.fxl * 3 + (f.ms_fx ? f.ms_fx - 1 : 0)); }));
  S.emplace_back(new LApm("bpos x match-expect x winner x cost-so-far", 8 * 64 * 2 * 6, [](const Feat& f) -> uint32_t { return ((f.bpos * 64 + f.ms_cm * 16 + f.ms_fx * 4 + f.ms_sm) * 2 + f.w1) * 6 + qcost6(f.cum); }));
  // 2-D reliability (forecast pairs) and vote counts (signal detection / wisdom-of-crowds nonlinearity)
  S.emplace_back(new LApm("d(tf) x d(fx)", 81, [](const Feat& f) -> uint32_t { return qd9(f.tf - f.stg) * 9 + qd9(f.fx - f.stg); }));
  S.emplace_back(new LApm("d(tf) x d(e2) x d(m1)", 729, [](const Feat& f) -> uint32_t { return (qd9(f.tf - f.stg) * 9 + qd9(f.e2 - f.stg)) * 9 + qd9(f.m1 - f.stg); }));
  S.emplace_back(new LApm("votes x bpos", 192, [](const Feat& f) -> uint32_t { return f.votes * 8 + f.bpos; }));
  S.emplace_back(new LApm("votes x d(median)", 216, [](const Feat& f) -> uint32_t { return f.votes * 9 + qd9(f.mmed - f.stg); }));
  S.emplace_back(new LApm("fixed-share weight of G1 x d(pool)", 90, [](const Feat& f) -> uint32_t { int a = (int)(f.fsw * 9.999f); return a * 9 + qd9(f.sfs - f.stg); }));
  S.emplace_back(new LApm("none", 1, [](const Feat&) -> uint32_t { return 0; }, 0.005f, 0.0004f, 512));
  S.emplace_back(new LApm("bpos x cost-so-far", 48, [](const Feat& f) -> uint32_t { return f.bpos * 6 + qcost6(f.cum); }, 0.005f, 0.0004f, 512));
  S.emplace_back(new LApm("d(tf) x d(fx)", 81, [](const Feat& f) -> uint32_t { return qd9(f.tf - f.stg) * 9 + qd9(f.fx - f.stg); }, 0.005f, 0.0004f, 512));
  S.emplace_back(new LApm("votes x d(median)", 216, [](const Feat& f) -> uint32_t { return f.votes * 9 + qd9(f.mmed - f.stg); }, 0.0005f, 0.00002f, 4096));
  // ---- K2: fixed-share arithmetic pool (BMA with switching) as an extra input ----
  auto in_fs = [](const Feat& f, float* x) { x[0] = f.stg; x[1] = f.sfs; };
  S.emplace_back(new MixK("G1 + fixed-share pool", 2, 1, [](const Feat&) -> uint32_t { return 0; }, in_fs, 0.0005f));
  S.emplace_back(new MixK("G1 + fixed-share pool, by bpos", 2, 8, bp, in_fs, 0.0005f));
  // ---- K3: covariance-aware combiner (EKF) ----
  auto in9 = [](const Feat& f, float* x) { x[0] = f.stg; x[1] = f.tf; x[2] = f.fx; x[3] = f.e2; x[4] = f.m1; x[5] = f.mmean; x[6] = f.spsse; x[7] = f.spfin; x[8] = f.sc3; x[9] = 0.5f; };
  auto in33 = [](const Feat& f, float* x) { x[0] = f.stg; for (int i = 0; i < 23; ++i) x[1 + i] = f.mix[i]; x[24] = f.tf; x[25] = f.fx; x[26] = f.e2; x[27] = f.m1; x[28] = f.spsse; x[29] = f.spfin; x[30] = f.sc3; x[31] = f.mmed; x[32] = 0.5f; };
  auto one = [](const Feat&) -> uint32_t { return 0; };
  S.emplace_back(new MixK("SGD control, 10 inputs", 10, 1, one, in9, 0.0003f));
  S.emplace_back(new Ekf("10 inputs", 10, 1, one, in9, 1e-4, 1e-10));
  S.emplace_back(new Ekf("10 inputs", 10, 1, one, in9, 1e-3, 1e-9));
  S.emplace_back(new Ekf("10 inputs", 10, 1, one, in9, 1e-3, 1e-8));
  S.emplace_back(new Ekf("10 inputs by bpos", 10, 8, bp, in9, 1e-3, 1e-9));
  S.emplace_back(new Ekf("10 inputs by bpos x cost-so-far", 10, 48, [](const Feat& f) -> uint32_t { return f.bpos * 6 + qcost6(f.cum); }, in9, 1e-3, 1e-9));
  S.emplace_back(new Ekf("10 inputs by votes", 10, 24, [](const Feat& f) -> uint32_t { return f.votes; }, in9, 1e-3, 1e-9));
  S.emplace_back(new Ekf("33 inputs (all layer-1 inputs)", 33, 1, one, in33, 1e-4, 1e-10));
  S.emplace_back(new Ekf("33 inputs (all layer-1 inputs)", 33, 1, one, in33, 1e-3, 1e-9));
  S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-8));
  S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-2, 1e-9));
  S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 0));
  S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-7));
  S.emplace_back(new Ekf("10 inputs", 10, 1, one, in9, 1e-3, 0));
  S.emplace_back(new Ekf("33 inputs by bpos", 33, 8, bp, in33, 1e-3, 1e-9));
  // ---- K4: regime-gated second GLN ----
  auto xin = [](const Feat& f, float* X) { X[0] = f.fx; X[1] = f.tf; X[2] = f.e2; X[3] = f.m1; X[4] = f.mmed; X[5] = f.sc3; X[6] = f.stg; X[7] = 0.5f; };
  auto z_same = [xin](const Feat& f, float* X, float* Z) { xin(f, X); Z[0] = f.stg * 0.25f; Z[1] = (f.tf - f.stg) * 0.5f; Z[2] = (f.fx - f.stg) * 0.5f; Z[3] = (f.sc3 - f.stg);
    Z[4] = (f.m1 - f.stg) * 0.5f; Z[5] = (f.e2 - f.stg) * 0.5f; Z[6] = (f.mmean - f.stg) * 0.5f; Z[7] = (f.spfin - f.stg); };
  auto z_reg = [xin](const Feat& f, float* X, float* Z) { xin(f, X); Z[0] = f.stg * 0.25f; Z[1] = (f.tf - f.stg) * 0.5f; Z[2] = (f.fx - f.stg) * 0.5f; Z[3] = std::min(f.cum, 8.f) * 0.25f - 0.3f;
    Z[4] = (f.ef - f.es) * 0.5f; Z[5] = std::max(-2.f, std::min(2.f, f.sk_tf)) * 0.7f - 0.2f; Z[6] = std::max(-2.f, std::min(2.f, f.sk_fx)) * 0.7f - 0.2f; Z[7] = (f.votes - 11.5f) * 0.12f; };
  auto z_reg2 = [xin](const Feat& f, float* X, float* Z) { xin(f, X); Z[0] = std::min(f.cum, 8.f) * 0.25f - 0.3f; Z[1] = (f.ef - 1.7f) * 0.5f; Z[2] = (f.es - 1.7f) * 0.8f; Z[3] = (f.bpos - 3.5f) * 0.3f;
    Z[4] = (f.fsw - 0.5f) * 2; Z[5] = std::max(-2.f, std::min(2.f, f.sk_tf)) * 0.7f - 0.2f; Z[6] = std::max(-2.f, std::min(2.f, f.sk_fx)) * 0.7f - 0.2f; Z[7] = (f.votes - 11.5f) * 0.12f; };
  auto z_vote = [xin](const Feat& f, float* X, float* Z) { xin(f, X); Z[0] = f.stg * 0.25f; Z[1] = (f.tf - f.stg) * 0.5f; Z[2] = (f.fx - f.stg) * 0.5f; Z[3] = (f.mmed - f.stg) * 0.5f;
    Z[4] = (f.m1 - f.stg) * 0.5f; Z[5] = (f.e2 - f.stg) * 0.5f; Z[6] = (f.sfs - f.stg) * 0.5f; Z[7] = (f.votes - 11.5f) * 0.12f; };
  { g1::Config c; c.seed = 77; S.emplace_back(new Gln2("control: same kind of gates as G1", c, z_same)); }
  { g1::Config c; c.seed = 77; S.emplace_back(new Gln2("regime gates (cost-so-far, EMA, skill, votes) + disagreement", c, z_reg)); }
  { g1::Config c; c.seed = 77; S.emplace_back(new Gln2("regime-only gates", c, z_reg2)); }
  { g1::Config c; c.seed = 77; S.emplace_back(new Gln2("vote/median/pool gates", c, z_vote)); }
  { g1::Config c; c.seed = 77; c.lr = 0.0005f; c.lrmin = 0.0001f; S.emplace_back(new Gln2("regime gates + disagreement", c, z_reg)); }
  { g1::Config c; c.seed = 77; c.lr = 0.0005f; c.lrmin = 0.0001f; S.emplace_back(new Gln2("vote/median/pool gates", c, z_vote)); }
  { g1::Config c; c.seed = 77; S.emplace_back(new Chain(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9), new Gln2("vote/median/pool gates", c, z_vote))); }
  { g1::Config c; c.seed = 77; S.emplace_back(new Chain(new Gln2("vote/median/pool gates", c, z_vote), new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9))); }
  S.emplace_back(new Chain(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9), new LApm("d(tf) x d(e2) x d(m1)", 729, [](const Feat& f) -> uint32_t { return (qd9(f.tf - f.stg) * 9 + qd9(f.e2 - f.stg)) * 9 + qd9(f.m1 - f.stg); })));
  if (getenv("X3B_SET") && atoi(getenv("X3B_SET")) == 2) {   // round 2: variants around the EKF hit
    S.clear();
    auto in41 = [in33](const Feat& f, float* x) { in33(f, x); for (int i = 0; i < 8; ++i) x[33 + i] = f.gn[i]; };
    auto in36 = [in33](const Feat& f, float* x) { in33(f, x); float a = std::fabs(f.tf - f.stg), b = std::fabs(f.fx - f.stg); x[33] = f.stg * std::min(a, 4.f) * 0.25f; x[34] = f.stg * std::min(b, 4.f) * 0.25f; x[35] = f.stg * std::min(std::fabs(f.stg), 8.f) * 0.125f; };
    auto conf3 = [](const Feat& f) -> uint32_t { float a = std::fabs(f.stg); return a < 1.5f ? 0 : a < 5 ? 1 : 2; };
    auto lap3 = [](const Feat& f) -> uint32_t { return (qd9(f.tf - f.stg) * 9 + qd9(f.e2 - f.stg)) * 9 + qd9(f.m1 - f.stg); };
    S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9));
    S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 3e-10));
    S.emplace_back(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 3e-9));
    S.emplace_back(new Ekf("33 + G1 neuron logits", 41, 1, one, in41, 1e-3, 1e-9));
    S.emplace_back(new Ekf("33 + 3 interaction terms", 36, 1, one, in36, 1e-3, 1e-9));
    S.emplace_back(new Ekf("33 inputs by confidence (3 sets)", 33, 3, conf3, in33, 1e-3, 1e-9));
    S.emplace_back(new Chain(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9), new LApm("d(tf) x d(e2) x d(m1)", 729, lap3, 0.001f, 0.00005f, 2048)));
    { g1::Config c; c.seed = 77; c.lr = 0.0005f; c.lrmin = 0.0001f; S.emplace_back(new Chain(new Chain(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9), new LApm("d(tf) x d(e2) x d(m1)", 729, lap3)), new Gln2("regime gates + disagreement", c, z_reg))); }
    { g1::Config c; c.seed = 77; c.lr = 0.0005f; c.lrmin = 0.0001f; S.emplace_back(new Chain(new Ekf("33 inputs", 33, 1, one, in33, 1e-3, 1e-9), new Gln2("regime gates + disagreement", c, z_reg))); }
  }
  const int NS = S.size();

  // ---- base: exact G1 (C4 variant) and K5 variants of G1 itself with an innovation-driven gain ----
  g1::Config gc; g1::Gln g(gc);
  const int NV = 3; const float vk[NV] = {0.5f, 1.0f, -0.5f};
  std::vector<std::unique_ptr<g1::Gln>> gv; for (int v = 0; v < NV; ++v) gv.emplace_back(new g1::Gln(gc));
  double vtot[NV] = {0};
  // fixed-share pool state
  const int NE = 7; double fw[NE]; for (int i = 0; i < NE; ++i) fw[i] = 1.0 / NE; const double fs_alpha = 0.002;

  std::vector<unsigned char> buf((size_t)R * 65536); size_t got; long bits = 0;
  double c3 = 0, c4 = 0, seg4[10] = {0}, fs_alone = 0; long nov = 0;
  Feat F; memset(&F, 0, sizeof F); F.ef = F.es = 1.5f; F.pc1 = F.pc2 = 1; F.sk_tf = 0.2f; F.sk_fx = 0.2f;
  float bc4 = 0, btf = 0, bfx = 0, be2 = 0; Rec rec; memset(&rec, 0, sizeof rec); float innov = 1.f;   // innov: EMA of C3 bit cost ratio
  float X[g1::KX], Z[g1::ZD];
  while (bits < total && (got = fread(buf.data(), R, std::min<long>(65536, total - bits), f)) > 0)
    for (size_t r = 0; r < got; ++r, ++bits) {
      const float* fl = (const float*)&buf[r * R]; const unsigned char* bb = &buf[r * R + 120];
      const int bit = bb[0]; const unsigned q = (unsigned)(fl[n + 3] * 65536.0f + 0.5f); unsigned q2 = q;
      int c0 = bb[2], bpos = 0; { int t = c0; while (t > 1) { t >>= 1; ++bpos; } }
      const int sgi = (int)(bits * 10 / total);
      if (bpos == 0) {   // byte start: fold the finished byte into the regime state
        if (bits) {
          F.ef += (bc4 - F.ef) * 0.25f; F.es += (bc4 - F.es) / 32; F.sk_tf += (btf - bc4 - F.sk_tf) * 0.125f; F.sk_fx += (bfx - bc4 - F.sk_fx) * 0.125f; F.sk_e2 += (be2 - bc4 - F.sk_e2) * 0.125f;
          F.w2 = F.w1; F.w1 = bfx < btf; F.haz = bc4 >= 4.f ? 0 : F.haz + 1; F.pc2 = F.pc1; F.pc1 = bc4;
        }
        bc4 = btf = bfx = be2 = 0;
        if (fc && fread(&rec, 40, 1, fc) != 1) memset(&rec, 0, sizeof rec);
        F.cml = rec.cm_len == 0 ? 0 : rec.cm_len <= 32 ? 1 : rec.cm_len <= 128 ? 2 : 3; F.fxl = rec.fx_len == 0 ? 0 : rec.fx_len <= 8 ? 1 : rec.fx_len <= 32 ? 2 : 3;
      }
      const double cb3 = bitcost(q, bit); c3 += cb3;
      if (bb[1]) { fs_alone += cb3; c4 += cb3; seg4[sgi] += cb3; for (int v = 0; v < NV; ++v) vtot[v] += cb3; for (int s = 0; s < NS; ++s) { S[s]->tot += cb3; S[s]->seg[sgi] += cb3; } ++nov; continue; }
      const float sq = g1::Stretch(q / 65536.0f);
      g1::Features(fl, nmix, fl[n], fl[n + 1], sq, bb[5], X, Z); Z[3] = g1::Stretch(fl[n + 2]) - sq;   // C4's z[3]
      const float stg = g.Predict(X, Z); const float pg = g.Squash(stg); q2 = quant(pg); g.Update(bit);
      const double cb4 = bitcost(q2, bit); c4 += cb4; seg4[sgi] += cb4;
      // K5: G1 with its learning rate scaled by an innovation signal (recent C3 bit cost over its long-run mean)
      for (int v = 0; v < NV; ++v) { g1::Gln& h = *gv[v]; float s = h.Predict(X, Z); vtot[v] += bitcost(quant(h.Squash(s)), bit);
        float mul = 1.f + vk[v] * std::max(-0.9f, std::min(3.f, innov - 1.f)); float lr0 = h.c_.lr, lm0 = h.c_.lrmin; h.c_.lr = lr0 * mul; h.c_.lrmin = lm0 * mul; h.Update(bit); h.c_.lr = lr0; h.c_.lrmin = lm0; }
      // features
      F.stg = clip12(stg); F.sc3 = sq; F.fx = fl[nmix]; F.tf = fl[nmix + 1]; F.e2 = fl[nmix + 2]; F.m1 = fl[n]; F.spsse = g1::Stretch(fl[n + 1]); F.spfin = g1::Stretch(fl[n + 2]);
      { float m = 0; int v = 0; float tmp[23]; for (int i = 0; i < nmix; ++i) { F.mix[i] = fl[i]; m += fl[i]; v += fl[i] > F.stg; tmp[i] = fl[i]; } F.mmean = m / nmix; F.votes = v;
        std::nth_element(tmp, tmp + 11, tmp + 23); F.mmed = tmp[11]; }
      for (int j = 0; j < 8; ++j) F.gn[j] = g1::Stretch(g.out_[j]);
      F.bpos = bpos; F.c0 = c0; F.b1 = bb[3]; F.b2 = bb[4]; F.cum = bc4;
      F.ms_cm = mstate(rec.cm_len, rec.cm_pred, c0, bpos); F.ms_fx = mstate(rec.fx_len, rec.fx_pred, c0, bpos); F.ms_sm = mstate(rec.sm_len, rec.sm_pred, c0, bpos);
      // fixed-share pool over {G1, tf, fx, e2, m1, mean mixer, psse}
      const float ep[NE] = {pg, sqsh(F.tf), sqsh(F.fx), sqsh(F.e2), sqsh(clip12(F.m1)), sqsh(F.mmean), fl[n + 1]};
      { double pm = 0; for (int i = 0; i < NE; ++i) pm += fw[i] * ep[i]; float pf = (float)std::min(1 - 1e-6, std::max(1e-6, pm)); F.sfs = g1::Stretch(pf); F.fsw = (float)fw[0]; fs_alone += bitcost(quant(pf), bit); }
      for (int s = 0; s < NS; ++s) { Stage& st = *S[s]; float x = st.predict(F); double c = bitcost(quant(sqsh(clip12(x))), bit); st.tot += c; st.seg[sgi] += c; st.update(bit); }
      { double z = 0; for (int i = 0; i < NE; ++i) { double pe = std::min(1 - 1e-6, std::max(1e-6, (double)(bit ? ep[i] : 1 - ep[i]))); fw[i] *= pe; z += fw[i]; }
        for (int i = 0; i < NE; ++i) fw[i] = (1 - fs_alpha) * fw[i] / z + fs_alpha / NE; }
      bc4 += (float)cb4; btf += (float)-std::log2(std::max(1e-6f, bit ? ep[1] : 1 - ep[1])); bfx += (float)-std::log2(std::max(1e-6f, bit ? ep[2] : 1 - ep[2])); be2 += (float)-std::log2(std::max(1e-6f, bit ? ep[3] : 1 - ep[3]));
      innov += ((float)cb3 / 0.22f - innov) * (1.f / 64);
    }
  printf("# x3b_sim %s: %ld bits (%ld override), C3 %.1f B, C4(G1 exact) %.1f B (G1 %+.1f B)\n", argv[1], bits, nov, c3 / 8, c4 / 8, (c4 - c3) / 8);
  printf("fixed-share arithmetic pool alone: %+.1f B vs C4\n", (fs_alone - (c4 - 0)) / 8);
  for (int v = 0; v < NV; ++v) printf("K5 G1 with innovation gain k=%+.1f: %+.1f B vs C4\n", vk[v], (vtot[v] - c4) / 8);
  printf("| stage | delta vs C4 (B) | %% | by tenth of the dump (B) |\n|---|---:|---:|---|\n");
  for (int s = 0; s < NS; ++s) { printf("| %s | %+.1f | %+.4f%% |", S[s]->name.c_str(), (S[s]->tot - c4) / 8, 100 * (S[s]->tot - c4) / c4);
    for (int i = 0; i < 10; ++i) printf(" %+.0f", (S[s]->seg[i] - seg4[i]) / 8); printf(" |\n"); }
  return 0;
}
