// G1 offline GLN simulator over a G1 dump (header "G1DUMP\0\0", u32 n; records: n+3 floats + 8 bytes).
// Record floats: x[0..n) layer-1 inputs (stretched), m1 raw final-mixer logit, p_sse, p_fin; bytes: bit, ovr, bitctx, c1, c2, e1_cb, e1_bb, e1_ctx.
// Prints baseline cost (p_fin) and GLN-variant cost, both with the coder's 16-bit discretization, in bytes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <cstdint>
struct Opt {
  int N1 = 16, K1 = 4, K2 = 4;       // layer-1 neurons, hyperplanes per layer-1 neuron, hyperplanes for the output neuron
  int bposgate = 1;                  // multiply contexts by bit position (8)
  int zmode = 0;                     // side info: 0 = all inputs + s(pfin), 1 = compact features
  int xmode = 0;                     // base inputs: 0 = all x + s(pfin) + bias, 1 = compact
  float lr1 = 0.002f, lr2 = 0.002f, lrmin = 0.0005f, tau = 64;  // per-context lr: lrmin + (lr - lrmin) * tau / (tau + n)
  float wclip = 8.f, bscale = 1.0f;
  int init2 = 0;                     // output neuron init: 0 = identity on s(q), 1 = mean of layer-1 logits
  int init = 1;                      // 0 = 1/K uniform, 1 = identity on s(pfin)
  int blend = 1;                     // 0 = GLN out directly, 1 = 2-input mixer [s(pfin), s(gln)] per bpos
  float lrb = 0.0005f;
  int skip_l1 = 0;                   // 1 = output neuron only (single-neuron GLN on base inputs)
  long limit = -1;
  int seed = 923;
  float lrmin2 = -1;                 // output neuron lrmin (default lrmin)
  int place = 1;                     // 1 = after the BITLSTM head (exact), 0 = before it (head delta held fixed)
  int report = 0;                    // print cumulative gain every report bits
};
static inline float sq(float x) { return 1.f / (1.f + std::exp(-x)); }
static inline float st(float p) { if (p < 1e-6f) p = 1e-6f; if (p > 1 - 1e-6f) p = 1 - 1e-6f; return std::log(p / (1 - p)); }
static inline double costbits(float p, int bit) {
  unsigned int P = 1 + 65534 * p;  // P(bit=1) * 65536 as in encoder.cpp (truncating)
  if (p >= 1) P = 65535; if (p <= 0) P = 1;
  return -std::log2(bit ? P / 65536.0 : (65536 - P) / 65536.0);
}
struct Neuron {
  int K, H, nctx; std::vector<float> w; std::vector<uint32_t> cnt; std::vector<float> v, b; int zd;
  float lr, lrmin, tau, wclip; int cur = 0; float out = 0, dot = 0;
  void init(int K_, int H_, int zd_, int ctxmul, std::mt19937& rng, float bscale, int idpos, float lr_, float lrmin_, float tau_, float wc) {
    K = K_; H = H_; zd = zd_; nctx = (1 << H) * ctxmul; lr = lr_; lrmin = lrmin_; tau = tau_; wclip = wc;
    w.assign((size_t)nctx * K, idpos < 0 ? 1.f / K : 0.f);
    if (idpos >= 0) for (int c = 0; c < nctx; ++c) w[(size_t)c * K + idpos] = 1.f;
    cnt.assign(nctx, 0);
    std::normal_distribution<float> nd(0, 1);
    v.resize(H * zd); b.resize(H);
    for (int h = 0; h < H; ++h) { float nn = 0; for (int i = 0; i < zd; ++i) { v[h * zd + i] = nd(rng); nn += v[h*zd+i]*v[h*zd+i]; }
      nn = std::sqrt(nn); for (int i = 0; i < zd; ++i) v[h * zd + i] /= nn; b[h] = nd(rng) * bscale; }
  }
  float predict(const float* x, const float* z, int extra) {
    int c = 0;
    for (int h = 0; h < H; ++h) { float d = 0; for (int i = 0; i < zd; ++i) d += v[h * zd + i] * z[i]; c = c * 2 + (d > b[h]); }
    cur = c + (1 << H) * extra;
    const float* wc = &w[(size_t)cur * K]; float d = 0; for (int i = 0; i < K; ++i) d += wc[i] * x[i];
    if (d > 12) d = 12; if (d < -12) d = -12; dot = d; out = sq(d); return d;
  }
  void update(const float* x, int bit) {
    float* wc = &w[(size_t)cur * K]; uint32_t n = cnt[cur]++;
    float l = lrmin + (lr - lrmin) * tau / (tau + n);
    float g = l * (out - bit);
    for (int i = 0; i < K; ++i) { float t = wc[i] - g * x[i]; if (t > wclip) t = wclip; if (t < -wclip) t = -wclip; wc[i] = t; }
  }
};
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: glnsim dump [key=val ...]\n"); return 2; }
  Opt o;
  for (int a = 2; a < argc; ++a) {
    char k[64]; float v; if (sscanf(argv[a], "%63[^=]=%f", k, &v) != 2) continue; std::string s = k;
#define F(name) else if (s == #name) o.name = v;
    if (0) {} F(N1) F(K1) F(K2) F(bposgate) F(zmode) F(xmode) F(lr1) F(lr2) F(lrmin) F(tau) F(wclip) F(bscale) F(init) F(blend) F(lrb) F(skip_l1) F(limit) F(report) F(place) F(init2) F(seed) F(lrmin2)
    else { fprintf(stderr, "unknown %s\n", k); return 2; }
  }
  FILE* f = fopen(argv[1], "rb"); char hdr[8]; uint32_t n;
  if (!f || fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "G1DUMP", 6) || fread(&n, 4, 1, f) != 1) { fprintf(stderr, "bad dump\n"); return 1; }
  const int R = (n + 4) * 4 + 8;
  std::vector<unsigned char> buf((size_t)R * 65536);
  // base inputs
  std::vector<float> X, Z, X2; std::vector<int> bit_dummy;
  int KX = o.xmode == 0 ? n + 2 : o.xmode == 2 ? 10 : 8;
  int ZD = o.zmode == 0 ? n + 1 : o.zmode == 2 ? 12 : 8;
  std::mt19937 rng(o.seed);
  int ctxmul = o.bposgate ? 8 : 1;
  std::vector<Neuron> L1(o.skip_l1 ? 0 : o.N1);
  for (int j = 0; j < (int)L1.size(); ++j) L1[j].init(KX, o.K1, ZD, ctxmul, rng, o.bscale, o.init ? KX - 2 : -1, o.lr1, o.lrmin, o.tau, o.wclip);
  int K2in = o.skip_l1 ? KX : o.N1 + 2;
  Neuron out; out.init(K2in, o.K2, ZD, ctxmul, rng, o.bscale, o.init ? K2in - 2 : -1, o.lr2, o.lrmin2 >= 0 ? o.lrmin2 : o.lrmin, o.tau, o.wclip);
  if (o.init2 && !o.skip_l1) for (int c = 0; c < out.nctx; ++c) for (int i = 0; i < K2in; ++i) out.w[(size_t)c * K2in + i] = i < o.N1 ? 1.f / o.N1 : 0.f;
  X.resize(KX); Z.resize(ZD); X2.resize(K2in);
  float bw[8][2]; for (int i = 0; i < 8; ++i) { bw[i][0] = 1; bw[i][1] = 0; }
  double base = 0, gln = 0, raw = 0; long bits = 0, used = 0; size_t got; int bpos = 0;
  double rep_base = 0, rep_gln = 0;
  while ((got = fread(buf.data(), R, 65536, f)) > 0) {
    for (size_t r = 0; r < got; ++r) {
      const unsigned char* rec = &buf[(size_t)r * R];
      const float* fl = (const float*)rec; const unsigned char* bb = rec + (n + 4) * 4;
      int bit = bb[0], ovr = bb[1], bc = bb[2];
      float pfin = fl[n + 2], q = fl[n + 3];
      float d_head = st(q) - st((1 + (unsigned)(65534 * pfin)) / 65536.0f);
      bpos = 0; { int t = bc; while (t > 1) { t >>= 1; ++bpos; } }  // bits seen so far in this byte
      if (ovr) { double cb = -std::log2(bit ? q : 1 - q); base += cb; gln += cb; raw += cb; ++bits; continue; }
      float sp = o.place ? st(q) : st(pfin);
      if (o.xmode == 0) { for (unsigned i = 0; i < n; ++i) X[i] = fl[i]; X[n] = sp; X[n + 1] = 0.5f; }
      else {
        // compact: fxcm, bytemixer, E2, bitl, mean of the 23 mixers, m1, s(psse), s(pfin) ... (s(pfin) at KX-2), bias last
        float m = 0; for (int i = 0; i < 23; ++i) m += fl[i]; m /= 23;
        if (o.xmode == 2) { float mx = -99, mn = 99; for (int i = 0; i < 23; ++i) { mx = std::max(mx, fl[i]); mn = std::min(mn, fl[i]); }
          X[0] = fl[23]; X[1] = fl[24]; X[2] = fl[25]; X[3] = fl[26]; X[4] = m; X[5] = st(fl[n + 1]); X[6] = mx; X[7] = mn; X[8] = sp; X[9] = 0.5f; }
        else { X[0] = fl[23]; X[1] = fl[24]; X[2] = fl[25]; X[3] = fl[26]; X[4] = m; X[5] = st(fl[n + 1]); X[6] = sp; X[7] = 0.5f; }
      }
      if (o.zmode == 0) { for (unsigned i = 0; i < n; ++i) Z[i] = fl[i] * 0.25f; Z[n] = sp * 0.25f; }
      else {
        float m = 0, mx = -99, mn = 99; for (int i = 0; i < 23; ++i) { m += fl[i]; mx = std::max(mx, fl[i]); mn = std::min(mn, fl[i]); } m /= 23;
        Z[0] = sp * 0.25f; Z[1] = (fl[24] - sp) * 0.5f; Z[2] = (fl[23] - sp) * 0.5f; Z[3] = (mx - mn) * 0.25f; Z[4] = (fl[26] - sp) * 0.5f;
        Z[5] = (fl[25] - sp) * 0.5f; Z[6] = (m - sp) * 0.5f; Z[7] = (bb[5] - 7.5f) * 0.2f;
        if (o.zmode == 2) { Z[8] = (bpos - 3.5f) * 0.4f; Z[9] = (bb[6] - 1.5f) * 0.6f; Z[10] = (st(fl[n + 2]) - sp) * 1.0f; Z[11] = (fl[n] - sp) * 0.5f; }
      }
      int extra = o.bposgate ? bpos : 0;
      float so;
      if (o.skip_l1) { so = out.predict(X.data(), Z.data(), extra); }
      else {
        for (int j = 0; j < o.N1; ++j) X2[j] = L1[j].predict(X.data(), Z.data(), extra);
        X2[o.N1] = sp; X2[o.N1 + 1] = 0.5f;
        so = out.predict(X2.data(), Z.data(), extra);
      }
      float pg = sq(so), pf;
      if (o.blend) { float d = bw[bpos][0] * sp + bw[bpos][1] * so; if (d > 12) d = 12; if (d < -12) d = -12; pf = sq(d);
        float g = o.lrb * (pf - bit); bw[bpos][0] -= g * sp; bw[bpos][1] -= g * so; }
      else pf = pg;
      double cb = -std::log2(bit ? q : 1 - q), cg = costbits(o.place ? pf : sq(st(pf) + d_head), bit);
      base += cb; gln += cg; raw += costbits(pg, bit); ++bits; ++used;
      rep_base += cb; rep_gln += cg;
      if (o.report && used % o.report == 0) { printf("  @%ld bits: base %.0f B gln %.0f B (%+.0f)\n", bits, rep_base / 8, rep_gln / 8, (rep_gln - rep_base) / 8); rep_base = rep_gln = 0; }
      if (!o.skip_l1) for (int j = 0; j < o.N1; ++j) L1[j].update(X.data(), bit);
      out.update(o.skip_l1 ? X.data() : X2.data(), bit);
      if (o.limit > 0 && bits >= o.limit) break;
    }
    if (o.limit > 0 && bits >= o.limit) break;
  }
  printf("n=%u bits=%ld used=%ld base %.1f B  gln %.1f B  delta %+.1f B (raw gln out %+.1f B)\n", n, bits, used, base / 8, gln / 8, (gln - base) / 8, (raw - base) / 8);
  return 0;
}
