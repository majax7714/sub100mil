// X3c proxy: a small, fast, ORDER-SENSITIVE online context-mixing coder (lpaq-like), used to price article
// orders on the full stream in minutes. It is NOT the record; its per-article cost change under a reordering is
// validated against the real stack on samples (docs/design/X3c-arrangement.md, "Proxy validation").
//
//   g++ -O2 -march=x86-64-v3 -std=c++17 -o build/exp/X3c/x3c_proxy scripts/analysis/x3c_proxy.cpp
//   x3c_proxy IN OUT.f64 [text|wrt] [tablebits=22]
// IN: slicetool-pre text ("text": segments start at "  <page>\n") or a WRT coded stream ("wrt": segments start at
// the coded "  <page>\n    <title>"). OUT.f64: one record per segment: (double start offset, double bits).
// Segment 0 is what precedes the first page; the last segment includes everything after the last page.
// Model: orders 1-6 + word + sparse contexts in nibble-bucketed, checked, always-replace hash tables of adaptive
// 16-bit probabilities with counts; two match models (min length 6 and 20, most recent occurrence); a logistic mixer with
// weight sets selected by order-0 state and by match length; two APMs. Float math: this is a simulator, not a codec.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static inline float squash(float x) { if (x > 20) x = 20; if (x < -20) x = -20; return 1.0f / (1.0f + std::exp(-x)); }
static float stretch_tab[4096];
static inline float stretchp(float p) { int i = (int)(p * 4096); if (i < 0) i = 0; if (i > 4095) i = 4095; return stretch_tab[i]; }

struct Slot { uint16_t p; uint8_t n; uint8_t pad; };  // p = P(1)*65536
struct Bucket { uint16_t chk; uint16_t pad; Slot s[15]; };  // 64 B
static_assert(sizeof(Bucket) == 64, "bucket");

struct Table {
  std::vector<Bucket> t; uint32_t mask; Bucket* cur; int limit;
  void init(int bits, int lim) { t.assign(size_t(1) << bits, Bucket()); mask = (1u << bits) - 1; limit = lim;
    for (auto& b : t) for (auto& s : b.s) s.p = 32768; cur = &t[0]; }
  void select(uint64_t h) {
    h *= 0x9E3779B97F4A7C15ull; uint32_t i = (h >> 40) & mask; uint16_t c = (h >> 20) & 0xffff;
    cur = &t[i];
    if (cur->chk != c) { cur->chk = c; for (auto& s : cur->s) { s.p = 32768; s.n = 0; } }
  }
};

struct APM {
  std::vector<float> t; int idx = 0; float w = 0;
  void init(int n) { t.resize(size_t(n) * 33); for (int i = 0; i < n; i++) for (int j = 0; j < 33; j++) t[i * 33 + j] = squash((j - 16) * 0.5f); }
  float pp(float st, int cx) { float x = (st + 8) * 2; if (x < 0) x = 0; if (x > 31.999f) x = 31.999f; int j = (int)x; w = x - j;
    idx = cx * 33 + j; return t[idx] * (1 - w) + t[idx + 1] * w; }
  void update(int y) { float r = 0.02f; t[idx] += (y - t[idx]) * r * (1 - w); t[idx + 1] += (y - t[idx + 1]) * r * w; }
};

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: x3c_proxy IN OUT.f64 [text|wrt] [tablebits]\n"); return 1; }
  std::string mode = argc > 3 ? argv[3] : "text";
  int tb = argc > 4 ? atoi(argv[4]) : 22;
  std::string pat = mode == "wrt" ? std::string("  L\xdf\x98N\n    L\xdf\x9bN") : std::string("  <page>\n");
  FILE* f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> buf(n + 8); if (fread(buf.data(), 1, n, f) != n) return 1; fclose(f);
  for (int i = 0; i < 4096; i++) { float p = (i + 0.5f) / 4096; stretch_tab[i] = std::log(p / (1 - p)); }

  const int NC = 9;  // o1..o6, word, sparse(skip1), o8
  const int NI = NC * 2 + 4 + 1;
  Table T[NC]; for (int i = 0; i < NC; i++) T[i].init(i == 0 ? 16 : tb, i < 2 ? 250 : 60);
  std::vector<float> o0(256 * 256 * 0 + 256, 0.5f);
  // match models
  const int MB = 22; std::vector<uint32_t> mt1(size_t(1) << MB, 0), mt2(size_t(1) << MB, 0);
  size_t mp1 = 0, mp2 = 0; int ml1 = 0, ml2 = 0;
  std::vector<float> msm1(64 * 2, 0.5f), msm2(64 * 2, 0.5f);
  // mixers
  const int NS1 = 256, NS2 = 64 * 3;
  std::vector<float> w1(size_t(NS1) * NI, 0.15f), w2(size_t(NS2) * NI, 0.15f);
  APM a1, a2; a1.init(256 * 256); a2.init(1 << 16);
  std::vector<double> segstart, segbits; segstart.push_back(0); segbits.push_back(0);
  double total = 0; float in[NI];
  uint64_t h[NC] = {0}; uint64_t word = 0, pword = 0;
  uint32_t c4 = 0; uint64_t c8 = 0;
  const float LR = 0.0025f;
  for (size_t i = 0; i < n; i++) {
    if (i + pat.size() <= n && i > 0 && memcmp(&buf[i], pat.data(), pat.size()) == 0) { segstart.push_back((double)i); segbits.push_back(0); }
    int c = buf[i];
    int exp1 = ml1 ? buf[mp1] : -1, exp2 = ml2 ? buf[mp2] : -1;
    int c0 = 1;
    double bits = 0;
    for (int b = 7; b >= 0; b--) {
      int y = (c >> b) & 1;
      if (b == 7 || b == 3) {
        for (int k = 0; k < NC; k++) T[k].select(h[k] * 16 + (b == 3 ? c0 : 0) + (uint64_t)(k + 1) * 0x100000000ull + (b == 3 ? 0x8000 : 0));
      }
      int si = (b >= 4) ? c0 - 1 : ((c0 & ((1 << (4 - b - 0)) - 1)) | (1 << (3 - b))) - 1;  // index in nibble tree 0..14
      // within a nibble: node id = 1,2-3,4-7,8-15 -> minus 1
      if (b < 4) { int nb = 3 - b; int v = c0 & ((1 << nb) - 1); si = ((1 << nb) | v) - 1; } else { int nb = 7 - b; int v = c0 & ((1 << nb) - 1); si = ((1 << nb) | v) - 1; }
      int ni = 0;
      Slot* sl[NC];
      for (int k = 0; k < NC; k++) {
        Slot* s = &T[k].cur->s[si]; sl[k] = s;
        float p = (s->p + 0.5f) / 65536.0f; float st = stretchp(p);
        in[ni++] = st; in[ni++] = s->n == 0 ? 0 : (p - 0.5f) * 2 * (s->n > 3 ? 1.5f : 0.7f);
      }
      // match inputs
      int mctx = 0;
      for (int m = 0; m < 2; m++) {
        int e = m ? exp2 : exp1; int ml = m ? ml2 : ml1; std::vector<float>& sm = m ? msm2 : msm1;
        if (e >= 0 && ((e | 256) >> (b + 1)) == c0) {
          int eb = (e >> b) & 1; int l = ml > 31 ? 31 : ml; float p = sm[(l * 2 + 1)];  // P(expected bit correct) at this length
          float st = stretchp(p); in[ni++] = eb ? st : -st; in[ni++] = eb ? (l > 15 ? 2.f : 1.f) : (l > 15 ? -2.f : -1.f);
          if (m == 0) mctx = l < 8 ? 1 : (l < 16 ? 2 : (l < 24 ? 3 : 4)); else mctx += 5 * (l < 24 ? 1 : 2);
        } else { in[ni++] = 0; in[ni++] = 0; }
      }
      in[ni++] = 0.3f;
      float* W1 = &w1[size_t(c0) * NI]; int s2 = (mctx % 5) + 5 * (mctx / 5); s2 = s2 * 8 + (7 - b); if (s2 >= NS2) s2 = NS2 - 1; float* W2 = &w2[size_t(s2) * NI];
      float d1 = 0, d2 = 0; for (int k = 0; k < NI; k++) { d1 += W1[k] * in[k]; d2 += W2[k] * in[k]; }
      float p1 = squash(d1), p2 = squash(d2);
      float stm = (d1 + d2) * 0.5f; float pm = squash(stm);
      float pa = a1.pp(stm, (c4 & 0xff) * 256 + c0); float pb = a2.pp(stm, ((c4 & 0xffff) * 0x9E37u >> 8 ^ c0 * 0x2545u) & 0xffff);
      float p = (pm * 2 + pa + pb) * 0.25f; if (p < 1e-5f) p = 1e-5f; if (p > 1 - 1e-5f) p = 1 - 1e-5f;
      bits -= std::log2(y ? p : 1 - p);
      // updates
      float e1 = (y - p1) * LR, e2 = (y - p2) * LR; for (int k = 0; k < NI; k++) { W1[k] += e1 * in[k]; W2[k] += e2 * in[k]; }
      a1.update(y); a2.update(y);
      for (int k = 0; k < NC; k++) { Slot* s = sl[k]; int lim = T[k].limit; int nn = s->n; int t = y ? 65535 : 0;
        s->p += (int)((t - (int)s->p) / (nn + 1.5f)); if (nn < lim) s->n = nn + 1; }
      for (int m = 0; m < 2; m++) {
        int e = m ? exp2 : exp1; int& ml = m ? ml2 : ml1; std::vector<float>& sm = m ? msm2 : msm1;
        if (e >= 0 && ((e | 256) >> (b + 1)) == c0) { int eb = (e >> b) & 1; int l = ml > 31 ? 31 : ml; float& q = sm[l * 2 + 1]; q += ((eb == y) - q) * 0.01f;
          if (eb != y) { ml = 0; if (m) exp2 = -1; else exp1 = -1; } }
      }
      c0 = c0 * 2 + y;
    }
    total += bits; segbits.back() += bits;
    // byte update
    c4 = (c4 << 8) | c; c8 = (c8 << 8) | c;
    bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128;
    if (alpha) word = (word + (c | 32) + 1) * 0x2F0B4C47ull; else if (word) { pword = word; word = 0; }
    h[0] = c4 & 0xff; h[1] = c4 & 0xffff; h[2] = c4 & 0xffffff; h[3] = c4; h[4] = c8 & 0xffffffffffull; h[5] = c8 & 0xffffffffffffull;
    h[6] = word * 31 + pword * 0x9E3779B1ull + 0x1234567; h[7] = (c8 & 0xffffff00ull) + 0xABCDEF000000ull; h[8] = c8 * 0xD6E8FEB86659FD93ull + (c8 >> 32);
    // match models: update tables with hash of last 6 / 20 bytes
    if (i >= 24) {
      uint64_t k1 = (c8 & 0xffffffffffffull) * 0x9E3779B97F4A7C15ull >> (64 - MB);
      uint64_t hh = 0; for (int j = 0; j < 20; j++) hh = (hh + buf[i - j]) * 0x2F0B4C47ull + 0x55; uint64_t k2 = hh * 0x9E3779B97F4A7C15ull >> (64 - MB);
      if (ml1 > 0) { mp1++; if (ml1 < 65535) ml1++; } else { mp1 = mt1[k1]; if (mp1) { int l = 0; while (l < 32 && mp1 > (size_t)l + 1 && buf[mp1 - 1 - l] == buf[i - l]) l++; ml1 = l >= 6 ? l : 0; } }
      if (ml2 > 0) { mp2++; if (ml2 < 65535) ml2++; } else { mp2 = mt2[k2]; if (mp2) { int l = 0; while (l < 40 && mp2 > (size_t)l + 1 && buf[mp2 - 1 - l] == buf[i - l]) l++; ml2 = l >= 20 ? l : 0; } }
      mt1[k1] = (uint32_t)(i + 1); mt2[k2] = (uint32_t)(i + 1);
    }
    if ((i & 0x3ffffff) == 0 && i) fprintf(stderr, "%zu MB: %.0f B\n", i >> 20, total / 8);
  }
  FILE* o = fopen(argv[2], "wb");
  for (size_t k = 0; k < segstart.size(); k++) { double r[2] = {segstart[k], segbits[k]}; fwrite(r, 8, 2, o); }
  fclose(o);
  printf("%s: %zu bytes, %zu segments, %.0f B (%.4f bpc)\n", argv[1], n, segstart.size(), total / 8, total / n);
  return 0;
}
