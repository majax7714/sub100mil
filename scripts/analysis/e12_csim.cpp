// E12: causal re-simulation with the real specialist (patches/exp/E12-arith/arith.h) on a per-byte cost dump.
//
//   g++ -O2 -march=x86-64-v3 -std=c++17 -I patches/exp/E12-arith -o build/exp/E12/e12_csim scripts/analysis/e12_csim.cpp
//   build/exp/E12/e12_csim COSTLOG english.dic [lr=0.002 ...]
//
// Unlike e12_sim (fed by e12_slots.py, whose slot regexes see the whole sentence), the specialist here only sees
// the bytes already coded: it WRT-decodes them, opens slots on left-context triggers and follows the coded bits.
// The final-stage mixer is the one the cmix integration uses (E12Mix in the patch): inputs stretch(p_record),
// bias, stretch(StateMap[class, q(p1), byte position, bit]), stretch(clamped p1); weight set by (class, state, bit).
// Baseline: the same mixer with stretch(p_record) and bias only, weight set by (class, bit), inside slots.
#include <chrono>
#include <fstream>

#include "arith.h"

struct Rec {
  uint8_t byte, cm_len, cm_pred, fx_len, fx_pred, sm_len, sm_pred, flags;
  uint16_t pbit[8];
  float cost, cost_tf, cost_fx, cost_ppmd;
};
static_assert(sizeof(Rec) == 40, "rec");

static std::map<std::string, double> P;
static double par(const char* k, double d) { auto it = P.find(k); return it == P.end() ? d : it->second; }
static inline double stretch(double p) { return std::log(p / (1 - p)); }
static inline double squash(double x) { if (x > 40) x = 40; if (x < -40) x = -40; return 1 / (1 + std::exp(-x)); }
static inline double clampp(double p, double e = 1e-6) { return p < e ? e : (p > 1 - e ? 1 - e : p); }

struct StateMap {
  std::vector<float> p; std::vector<uint16_t> n; int limit;
  StateMap(size_t sz, int lim) : p(sz, 0.5f), n(sz, 0), limit(lim) {}
  void upd(size_t i, int y) { if (n[i] < limit) n[i]++; p[i] += (y - p[i]) / (n[i] + 0.5); }
};
struct Mixer {
  int nin; double lr; std::vector<double> w;
  Mixer(int ni, int nsets, double l) : nin(ni), lr(l), w((size_t)ni * nsets, 0.0) {
    for (int s = 0; s < nsets; s++) w[(size_t)s * ni] = 1.0;
  }
  double dot(int s, const double* x) const { double d = 0; for (int i = 0; i < nin; i++) d += w[(size_t)s * nin + i] * x[i]; return d; }
  void upd(int s, const double* x, double err) { for (int i = 0; i < nin; i++) w[(size_t)s * nin + i] += lr * err * x[i]; }
};
static int qp(double p1) {
  if (p1 <= 0) return 0;
  if (p1 >= 1) return 13;
  double st = stretch(clampp(p1, 1e-5));
  int q = (int)std::floor((st + 9) / 1.5) + 1;
  return q < 1 ? 1 : q > 12 ? 12 : q;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: e12_csim COSTLOG english.dic [k=v ...]\n"); return 1; }
  for (int i = 3; i < argc; i++) { const char* e = strchr(argv[i], '='); if (e) P[std::string(argv[i], (size_t)(e - argv[i]))] = atof(e + 1); }
  const double lr = par("lr", 0.002), eps = par("eps", 1e-4);
  const int smlim = (int)par("smlim", 1023);
  std::ifstream f(argv[1], std::ios::binary);
  f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(16);
  size_t n = (sz - 16) / 40;
  std::vector<Rec> R(n);
  f.read((char*)R.data(), n * 40);
  e12::ArithSpec spec(argv[2]);
  if (!spec.ok) { fprintf(stderr, "dictionary?\n"); return 1; }
  const int NC = e12::NCLS, NPB = 4;
  StateMap sm((size_t)NC * 14 * NPB * 8, smlim);
  Mixer mx(4, NC * 4 * 8, lr), mb(2, NC * 8, lr);
  struct Acc { double rec = 0, base = 0, full = 0; long bytes = 0; };
  std::vector<Acc> by(NC);
  Acc all;
  auto t0 = std::chrono::steady_clock::now();
  double spec_s = 0;
  for (size_t i = 0; i < n; i++) {
    const Rec& r = R[i];
    int cls0 = -1;
    for (int b = 0; b < 8; b++) {
      int y = (r.byte >> (7 - b)) & 1;
      double pr = clampp(r.pbit[b] / 65536.0);
      double pf = pr, pb = pr;
      int cls;
      auto ts = std::chrono::steady_clock::now();
      bool in = spec.InSlot(&cls);
      if (in) {
        if (b == 0) cls0 = cls;
        double p1; int st = 3, bp = 0, c2;
        double xs[4] = {stretch(pr), 0.3, 0, 0};
        int smi = -1;
        if (spec.Predict(&p1, &c2, &st, &bp)) {
          int pbk = bp == 0 ? 0 : bp == 1 ? 1 : bp < 4 ? 2 : 3;
          smi = ((cls * 14 + qp(p1)) * NPB + pbk) * 8 + b;
          xs[2] = stretch(clampp(sm.p[smi], 1e-6));
          xs[3] = stretch(clampp(p1, eps));
        }
        int set = (cls * 4 + st) * 8 + b, bset = cls * 8 + b;
        double xb[2] = {xs[0], xs[1]};
        pf = clampp(squash(mx.dot(set, xs)));
        pb = clampp(squash(mb.dot(bset, xb)));
        mx.upd(set, xs, y - pf);
        mb.upd(bset, xb, y - pb);
        if (smi >= 0) sm.upd(smi, y);
      }
      spec.Bit(y);
      spec_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
      double cr = -std::log2(y ? pr : 1 - pr), cb = -std::log2(y ? pb : 1 - pb), cf = -std::log2(y ? pf : 1 - pf);
      all.rec += cr; all.base += cb; all.full += cf;
      if (cls0 >= 0) { by[cls0].rec += cr; by[cls0].base += cb; by[cls0].full += cf; }
    }
    if (cls0 >= 0) by[cls0].bytes++;
    all.bytes++;
  }
  double tot_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("coded bytes %zu: record %.0f B, baseline %.0f B, full %.0f B, gain (base-full) %+.0f B, gain vs record %+.0f B\n",
         n, all.rec / 8, all.base / 8, all.full / 8, (all.base - all.full) / 8, (all.rec - all.full) / 8);
  printf("specialist time %.2f s (%.3f us/byte), total %.1f s\n", spec_s, spec_s * 1e6 / n, tot_s);
  printf("%-11s %8s %8s %10s %10s %10s %9s\n", "class", "opened", "bytes", "rec B", "base B", "full B", "gain B");
  for (int c = 0; c < NC; c++) {
    if (!spec.slots_opened[c]) continue;
    printf("%-11s %8ld %8ld %10.0f %10.0f %10.0f %+9.0f\n", e12::kClassNames[c], spec.slots_opened[c], by[c].bytes,
           by[c].rec / 8, by[c].base / 8, by[c].full / 8, (by[c].base - by[c].full) / 8);
  }
  return 0;
}
