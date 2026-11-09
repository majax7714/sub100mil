// N1 hidden-state cache: offline simulator on top of the C3 E2 logit head.
//
// Input: an A3 dump (patches/exp/A3-encoder-select, A3_DUMP=path): per transformer-predicted token
//   int32 target y (token id = vocab rank), float xn[192] (normalized final hidden state),
//   float raw_logits[205] (static head, pre-softcap).
// The E2 logit head is replayed exactly as scripts/analysis/a3_sim.cpp does (C3 rates by default:
// a0 0.25, af 0.05, atau 8000). On top of it, a continuous cache (Grave et al. 2017):
//   key_i = normalize(P xn_i) (P = identity when N1_KD=0, else a fixed seeded Gaussian KD x 192 projection),
//   value_i = y_i, over the last W tokens;
//   p_c(w) = sum_i exp(theta (cos(key_t, key_i) - 1)) [y_i = w] / sum_i exp(...).
// Combinations evaluated (all online and causal; gate contexts g = maxcos bucket x cache-confidence bucket):
//   LIN: p = (1 - lam_g) p_head + lam_g p_c            (prob-space interpolation, lam_g = sigmoid(z_g))
//   LOG: p ~ p_head * (p_c + eps)^beta_g               (logit-space: beta_g * log(p_c + eps) added to the logits)
//   FIN: p(y) = (1 - lam_g) p_rec(y) + lam_g p_c(y)     (final-stage proxy: p_rec = 2^-cost from a per-byte cost
//                                                        dump, N1_COST; exact as a byte-level mixture of the coder's
//                                                        final model, decomposable bit by bit)
// Also a hashed hidden-state count model (LSH, N1_LSH=bits): context = sign bits of N1_LSH random projections of
// xn, adaptive counts per context, combined the same three ways.
// Losses in nats (head/LIN/LOG) and bits (FIN). Prints totals; "bytes" = nats / ln 2 / 8.
//
// Env: N1_W (2048), N1_KD (0), N1_THETAS ("4,8,16,32,64"), N1_GLR (0.02), N1_BLR (0.02), N1_EPS (1e-3),
//      N1_A0/N1_AF/N1_ATAU (head schedule, C3: 0.25/0.05/8000), N1_COST (cost dump), N1_LSH (0 = off),
//      N1_LSHR (count adaptation floor 1/N, default 0.02), N1_NOCACHE (1 = skip the cache, LSH only),
//      N1_LIMIT (tokens).
// Build: O=build/exp/A3/tree/cpp_infer/src/opt
//   g++ -O3 -march=x86-64-v3 -std=c++17 -fno-math-errno -I$O scripts/analysis/n1_sim.cpp $O/glue.cpp -o build/exp/N1/n1_sim
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "glue.h"

namespace {
constexpr int D = 192, V = 205, VP = 208;
constexpr size_t REC = 4 + 4 * D + 4 * V;

struct Dump {
  const uint8_t* base = nullptr;
  size_t n = 0;
  int y(size_t i) const { int32_t v; std::memcpy(&v, base + i * REC, 4); return v; }
  const float* xn(size_t i) const { return reinterpret_cast<const float*>(base + i * REC + 4); }
  const float* raw(size_t i) const { return reinterpret_cast<const float*>(base + i * REC + 4 + 4 * D); }
};

Dump open_dump(const char* path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) { perror(path); exit(1); }
  struct stat st; fstat(fd, &st);
  if (st.st_size % REC) { fprintf(stderr, "dump size not a multiple of %zu\n", REC); exit(1); }
  Dump d;
  d.n = st.st_size / REC;
  d.base = static_cast<const uint8_t*>(mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
  madvise((void*)d.base, st.st_size, MADV_SEQUENTIAL);
  return d;
}

double A0 = 0.25, AF = 0.05, ATAU = 8000.0;

struct Head {
  std::vector<float> A = std::vector<float>(size_t(D + 1) * VP, 0.0f);
  // predict + update; p receives the head's distribution (208, pads 0)
  void step(const float* xn, const float* raw, int y, uint64_t t, float* p) {
    alignas(64) float l[VP];
    std::memcpy(l, raw, sizeof(float) * V);
    l[205] = l[206] = l[207] = 0.0f;
    for (int i = 0; i < D; i++) {
      const float a = xn[i];
      const float* row = A.data() + size_t(i) * VP;
      for (int o = 0; o < VP; o++) l[o] += row[o] * a;
    }
    const float* bias = A.data() + size_t(D) * VP;
    for (int o = 0; o < VP; o++) l[o] += bias[o];
    fx2::opt::head_softcap_softmax(l, p);
    alignas(64) float dl[VP];
    for (int o = 0; o < V; o++) {
      const float d = p[o] - (o == y ? 1.0f : 0.0f);
      const float c = l[o] * (1.0f / 15.0f);
      dl[o] = d * (1.0f - c * c);
    }
    dl[205] = dl[206] = dl[207] = 0.0f;
    const float lr = float(AF + (A0 - AF) / (1.0 + double(t) / ATAU)) * (1.0f / float(D + 2));
    float* Aw = A.data();
    for (int i = 0; i < D; i++) {
      const float c = lr * xn[i];
      float* row = Aw + size_t(i) * VP;
      for (int o = 0; o < VP; o++) row[o] -= c * dl[o];
    }
    float* bw = Aw + size_t(D) * VP;
    for (int o = 0; o < VP; o++) bw[o] -= lr * dl[o];
  }
};

std::vector<float> parse_list(const char* s) {
  std::vector<float> v;
  for (const char* p = s; *p;) { char* e; v.push_back(strtof(p, &e)); p = e; if (*p == ',') p++; }
  return v;
}
double envd(const char* k, double def) { const char* e = getenv(k); return e && *e ? atof(e) : def; }

// vectorizable exp for x <= 0 (sim only; rel err ~1e-4)
inline float fexp(float x) {
  x = std::max(x, -80.0f);
  const float t = x * 1.44269504f;
  const float fi = std::floor(t);
  const float f = t - fi;
  const float p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
  int32_t e = int32_t(fi) + 127;
  e = std::max(e, 1);
  float s; uint32_t u = uint32_t(e) << 23; std::memcpy(&s, &u, 4);
  return p * s;
}
inline float sigm(float z) { return 1.0f / (1.0f + std::exp(-z)); }

uint64_t rng = 0x9E3779B97F4A7C15ull;
double gauss() {
  auto u = [] { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return ((rng >> 11) + 0.5) / 9007199254740992.0; };
  const double a = u(), b = u();
  return std::sqrt(-2 * std::log(a)) * std::cos(6.283185307179586 * b);
}

// gate contexts: 16 maxcos/confidence buckets x 4 cache-confidence buckets
constexpr int NG = 64;
int gate_ctx(float conf1, float conf2) {
  int a = std::clamp(int(conf1 * 16.0f), 0, 15);
  int b = conf2 < 0.3f ? 0 : conf2 < 0.6f ? 1 : conf2 < 0.9f ? 2 : 3;
  return a * 4 + b;
}

struct Combo {  // the three combinations for one auxiliary distribution
  float zl[NG], zb[NG], zf[NG];
  double lin = 0, log_ = 0, fin = 0, alone = 0;
  Combo();
};

double GLR = 0.02, BLR = 0.02, EPS = 1e-3, Z0 = -5;
Combo::Combo() { for (int g = 0; g < NG; g++) { zl[g] = float(Z0); zb[g] = 0.0f; zf[g] = float(Z0); } }

// q: aux distribution (sums to 1 over V or is all-zero); ph: head distribution; prec: record's p(y) (or <0)
void combine(Combo& c, int g, const float* q, const float* ph, int y, double prec) {
  const double a = std::max(double(ph[y]), 1e-30), b = q[y];
  c.alone += -std::log(std::max(b * 0.999 + 0.001 / V, 1e-30));
  {  // LIN
    const double lam = sigm(c.zl[g]);
    const double p = (1 - lam) * a + lam * b;
    c.lin += -std::log(p);
    c.zl[g] -= float(GLR * (-(b - a) * lam * (1 - lam) / p));
    c.zl[g] = std::clamp(c.zl[g], -12.0f, 6.0f);
  }
  {  // LOG
    const float be = c.zb[g];
    double z = 0, el = 0;
    double ly = 0;
    for (int w = 0; w < V; w++) {
      const double lc = std::log(double(q[w]) + EPS);
      const double u = double(ph[w]) * std::exp(be * lc);
      z += u; el += u * lc;
      if (w == y) ly = lc;
    }
    const double p = double(ph[y]) * std::exp(be * ly) / z;
    c.log_ += -std::log(std::max(p, 1e-30));
    const double grad = -(ly - el / z);
    c.zb[g] = std::clamp(float(be - BLR * grad), 0.0f, 3.0f);
  }
  if (prec >= 0) {  // FIN
    const double lam = sigm(c.zf[g]);
    const double p = (1 - lam) * prec + lam * b;
    c.fin += -std::log2(p);
    c.zf[g] -= float(GLR * (-(b - prec) * lam * (1 - lam) / p));
    c.zf[g] = std::clamp(c.zf[g], -12.0f, 6.0f);
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: n1_sim DUMP (env: see header)\n"); return 2; }
  A0 = envd("N1_A0", 0.25); AF = envd("N1_AF", 0.05); ATAU = envd("N1_ATAU", 8000);
  Z0 = envd("N1_Z0", -5); const int CENTER = int(envd("N1_CENTER", 0)); const double CR = envd("N1_CR", 1e-3);
  GLR = envd("N1_GLR", 0.02); BLR = envd("N1_BLR", 0.0005); EPS = envd("N1_EPS", 1e-3);
  const int W = int(envd("N1_W", 2048));
  const int KD0 = int(envd("N1_KD", 0));
  const int KD = KD0 ? KD0 : D;
  const int LSH = int(envd("N1_LSH", 0));
  const double LSHR = envd("N1_LSHR", 0.02);
  const bool nocache = envd("N1_NOCACHE", 0) != 0;
  std::vector<float> th = parse_list(getenv("N1_THETAS") ? getenv("N1_THETAS") : "4,8,16,32,64");
  const int NT = nocache ? 0 : int(th.size());
  Dump d = open_dump(argv[1]);
  size_t N = d.n;
  if (const char* e = getenv("N1_LIMIT")) N = std::min(N, size_t(atoll(e)));

  // optional cost dump, aligned greedily by byte value (cost dump has the piece-start bytes the head skips)
  const char* cpath = getenv("N1_COST");
  const uint8_t* cbase = nullptr; size_t cn = 0;
  if (cpath && *cpath) {
    int fd = open(cpath, O_RDONLY); if (fd < 0) { perror(cpath); return 1; }
    struct stat st; fstat(fd, &st);
    cbase = static_cast<const uint8_t*>(mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    cbase += 16; cn = (st.st_size - 16) / 40;
  }
  static const unsigned char kVocab[32] = {0xe8,0x16,0x04,0x00,0xff,0xff,0xff,0x03,0x01,0xfc,0x0f,0xf9,0xfe,0xff,0xff,0x07,
                                           0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  int tok2byte[V]; { int r = 0; for (int b = 0; b < 256; b++) if ((kVocab[b / 8] >> (b % 8)) & 1) tok2byte[r++] = b; }

  // projection
  std::vector<float> P;
  if (KD0) { P.resize(size_t(KD) * D); for (auto& v : P) v = float(gauss()); }
  std::vector<float> keys(size_t(W) * KD, 0.0f);
  std::vector<int> vals(W, 0);
  // LSH
  std::vector<float> LP;
  std::vector<float> lcnt; std::vector<float> ltot;
  if (LSH) { LP.resize(size_t(LSH) * D); for (auto& v : LP) v = float(gauss()); lcnt.assign((size_t(1) << LSH) * V, 0.0f); ltot.assign(size_t(1) << LSH, 0.0f); }

  Head h;
  std::vector<float> mu(D, 0.0f), var(D, 1.0f);
  std::vector<Combo> cc(NT), lc(LSH ? 1 : 0);
  double hl = 0, sl = 0, rec = 0, maxcos_sum = 0;
  size_t ci = 0, skipped = 0, fin_n = 0;
  alignas(64) float ph[VP], ps[VP], key[512];
  std::vector<float> cosv(W), q(size_t(std::max(NT, 1)) * VP);
  std::vector<double> mcbucket(17, 0), mcgain(17, 0);
  for (size_t t = 0; t < N; t++) {
    const int y = d.y(t);
    const float* xn = d.xn(t);
    {  // static loss
      alignas(64) float l[VP]; std::memcpy(l, d.raw(t), 4 * V); l[205] = l[206] = l[207] = 0;
      fx2::opt::head_softcap_softmax(l, ps);
      sl += -std::log(std::max(double(ps[y]), 1e-30));
    }
    h.step(xn, d.raw(t), y, t, ph);
    hl += -std::log(std::max(double(ph[y]), 1e-30));
    double prec = -1;
    if (cbase) {
      while (ci < cn && cbase[ci * 40] != tok2byte[y]) { ci++; skipped++; }
      if (ci < cn) { float c; std::memcpy(&c, cbase + ci * 40 + 24, 4); prec = std::exp2(-double(c)); rec += c; fin_n++; ci++; }
    }
    // key (optionally centered / diagonally whitened by causal EMA statistics of xn)
    alignas(64) float xc[D];
    if (CENTER) {
      for (int i = 0; i < D; i++) xc[i] = xn[i] - mu[i];
      if (CENTER > 1) for (int i = 0; i < D; i++) xc[i] /= std::sqrt(var[i] + 1e-3f);
      for (int i = 0; i < D; i++) { const float dd = xn[i] - mu[i]; mu[i] += float(CR) * dd; var[i] += float(CR) * (dd * dd - var[i]); }
      xn = xc;
    }
    if (KD0) { for (int k = 0; k < KD; k++) { float s = 0; const float* pr = P.data() + size_t(k) * D; for (int i = 0; i < D; i++) s += pr[i] * xn[i]; key[k] = s; } }
    else std::memcpy(key, xn, 4 * D);
    { float s = 0; for (int k = 0; k < KD; k++) s += key[k] * key[k]; s = 1.0f / std::sqrt(s + 1e-12f); for (int k = 0; k < KD; k++) key[k] *= s; }
    if (NT) {
      const int n = int(std::min<size_t>(t, W));
      float mc = -1;
      for (int j = 0; j < n; j++) {
        const float* kk = keys.data() + size_t(j) * KD;
        float s = 0;
        for (int k = 0; k < KD; k++) s += kk[k] * key[k];
        cosv[j] = s; mc = std::max(mc, s);
      }
      if (n == 0) mc = 0;
      maxcos_sum += mc;
      for (int a = 0; a < NT; a++) {
        float* qa = q.data() + size_t(a) * VP;
        std::fill(qa, qa + VP, 0.0f);
        if (n) {
          const float tt = th[a];
          float z = 0;
          for (int j = 0; j < n; j++) { const float w = fexp(tt * (cosv[j] - mc)); qa[vals[j]] += w; z += w; }
          const float iz = 1.0f / z;
          for (int w = 0; w < V; w++) qa[w] *= iz;
        }
        float conf = 0; for (int w = 0; w < V; w++) conf = std::max(conf, qa[w]);
        const int g = gate_ctx(std::max(mc, 0.0f), conf);
        const double before = cc[a].lin;
        combine(cc[a], g, qa, ph, y, prec);
        if (a == NT / 2) { int bk = std::clamp(int(std::max(mc, 0.0f) * 16), 0, 16); mcbucket[bk] += 1; mcgain[bk] += (cc[a].lin - before) + std::log(std::max(double(ph[y]), 1e-30)); }
      }
      // insert
      const int slot = int(t % W);
      std::memcpy(keys.data() + size_t(slot) * KD, key, 4 * KD);
      vals[slot] = y;
    }
    if (LSH) {
      uint32_t hh = 0;
      for (int b = 0; b < LSH; b++) { float s = 0; const float* pr = LP.data() + size_t(b) * D; for (int i = 0; i < D; i++) s += pr[i] * xn[i]; hh = hh * 2 + (s > 0); }
      float* cnt = lcnt.data() + size_t(hh) * V;  // adaptive probabilities of this context
      alignas(64) float qa[VP] = {0};
      const float tot = ltot[hh];                  // experience count
      float conf = 0;
      if (tot > 0) { for (int w = 0; w < V; w++) { qa[w] = cnt[w]; conf = std::max(conf, qa[w]); } }
      const float cf = std::min(1.0f, std::log2(1.0f + tot) / 8.0f);  // context experience as the first gate key
      combine(lc[0], gate_ctx(cf, conf), qa, ph, y, prec);
      const float r = tot == 0 ? 1.0f : std::max(1.0f / (tot + 1.5f), float(LSHR));
      for (int w = 0; w < V; w++) cnt[w] *= (1 - r);
      cnt[y] += r; ltot[hh] = tot + 1;
    }
  }
  const double L2 = std::log(2.0);
  printf("center %d z0 %g glr %g\n", CENTER, Z0, GLR);
  printf("tokens %zu W %d KD %d LSH %d  static %.1f B  head %.1f B (head-static %.1f)\n", N, W, KD, LSH, sl / L2 / 8, hl / L2 / 8, (hl - sl) / L2 / 8);
  if (cbase) printf("cost dump: matched %zu skipped %zu  record %.1f B\n", fin_n, skipped, rec / 8);
  if (NT) printf("mean maxcos %.4f\n", maxcos_sum / N);
  for (int a = 0; a < NT; a++)
    printf("cache theta %-5g alone %.1f B | LIN %+.1f B | LOG %+.1f B | FIN %+.1f B (vs head / record)\n", th[a], cc[a].alone / L2 / 8,
           (cc[a].lin - hl) / L2 / 8, (cc[a].log_ - hl) / L2 / 8, cbase ? cc[a].fin / 8 - rec / 8 : 0.0);
  if (LSH) printf("lsh %d alone %.1f B | LIN %+.1f B | LOG %+.1f B | FIN %+.1f B\n", LSH, lc[0].alone / L2 / 8, (lc[0].lin - hl) / L2 / 8,
                  (lc[0].log_ - hl) / L2 / 8, cbase ? lc[0].fin / 8 - rec / 8 : 0.0);
  if (NT) {
    printf("LIN gain by maxcos bucket (theta %g): ", th[NT / 2]);
    for (int b = 0; b <= 16; b++) if (mcbucket[b] > 0) printf("[%.2f: n %.0f, %+.1f B] ", b / 16.0, mcbucket[b], mcgain[b] / L2 / 8);
    printf("\n");
  }
  return 0;
}
