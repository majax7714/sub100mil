// E12: offline arithmetic-consistency specialist on top of the record's per-byte costs.
//
//   g++ -O2 -march=x86-64-v3 -o build/exp/E12/e12_sim scripts/analysis/e12_sim.cpp
//   build/exp/E12/e12_sim COSTLOG SLOTS.bin [key=value ...]          (SLOTS from scripts/analysis/e12_slots.py)
//
// A slot is a number whose value the specialist can (partly) derive from numbers earlier in the same article,
// plus the byte that ends it. e12_slots.py describes, per slot, a weighted candidate set (explicit strings, or a
// lattice of values scale*k/D or scale*k/(D-k) rounded to `dec` decimals). Here each slot's candidates are
// expanded, sorted, and followed bit by bit along the coded path: at each bit the specialist's masses (m0, m1)
// are the candidate weights consistent with the bits coded so far that continue with 0 / 1. Once no candidate is
// consistent, the specialist abstains for the rest of the slot.
//
// Mixing (A2/E11's method): a logistic mixer with inputs stretch(p_record), a bias, and (when the specialist has
// an opinion) stretch(StateMap[class, q(p1), byte position bucket]) and stretch(clamped p1); weight set selected
// by (in slot?, class, specialist state: deterministic 0 / deterministic 1 / both / abstain).
// Baseline = the same mixer with only stretch(p_record) + bias, weight set selected by (in slot?, class), so the
// recalibration the slot trigger alone buys is not credited to the specialist.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

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

static const char* CLASSES[] = {
    "race%", "race%last", "hisp%", "hh_copy", "hh%", "hh%cons", "hhsize", "famsize", "age%", "age%last",
    "sexratio", "sexratio18", "area_mi", "land_km", "land_mi", "water_km", "water_mi", "water%",
    "dens_km", "dens_mi", "hdens_km", "hdens_mi", "coord", "conv",
};
static const int NC = 24;

struct Cand { std::string s; double w; };
struct Slot {
  int64_t start; int cls; std::string actual; int D;
  std::vector<Cand> c;  // sorted by s, merged
};

template <class T> static T rd(std::ifstream& f) { T v; f.read((char*)&v, sizeof v); return v; }

static void add_lattice(std::map<std::string, double>& m, int kind, double cw, int64_t D, int64_t kmin, int64_t kmax,
                        double scale, int dec, char term, double lo, double hi) {
  std::map<std::string, double> loc; double tot = 0; char buf[64];
  if (kmin < 0) kmin = 0;
  for (int64_t k = kmin; k <= kmax; k++) {
    double v;
    if (kind == 1) v = scale * (double)k / (double)D;
    else { if (k >= D) break; v = scale * (double)k / (double)(D - k); }
    if (v < lo - 1e-9 || v > hi + 1e-9) continue;
    int l = snprintf(buf, sizeof buf, "%.*f", dec, v);
    buf[l] = term;
    loc[std::string(buf, l + 1)] += 1; tot += 1;
  }
  if (tot <= 0) return;
  for (auto& kv : loc) m[kv.first] += cw * kv.second / tot;
}

static std::vector<Slot> load_slots(const char* path) {
  std::ifstream f(path, std::ios::binary);
  int64_t n = rd<int64_t>(f);
  std::vector<Slot> S(n);
  for (int64_t i = 0; i < n; i++) {
    Slot& s = S[i];
    s.start = rd<int64_t>(f); s.cls = rd<uint8_t>(f); int nc = rd<uint8_t>(f); int al = rd<uint8_t>(f);
    s.actual.resize(al); f.read(&s.actual[0], al);
    s.D = rd<int32_t>(f);
    std::map<std::string, double> m; double wsum = 0;
    std::vector<std::pair<std::map<std::string, double>, double>> comps;
    for (int c = 0; c < nc; c++) {
      int kind = rd<uint8_t>(f); float w = rd<float>(f);
      std::map<std::string, double> loc;
      if (kind == 0) {
        int cnt = rd<uint16_t>(f); double t = 0;
        for (int j = 0; j < cnt; j++) {
          int l = rd<uint8_t>(f); std::string st(l, 0); f.read(&st[0], l); float wt = rd<float>(f);
          loc[st] += wt; t += wt;
        }
        if (t > 0) for (auto& kv : loc) kv.second /= t;
      } else {
        int64_t D = rd<int64_t>(f), kmin = rd<int64_t>(f), kmax = rd<int64_t>(f); double scale = rd<double>(f);
        int dec = rd<uint8_t>(f); char term = (char)rd<uint8_t>(f); double lo = rd<double>(f), hi = rd<double>(f);
        add_lattice(loc, kind, 1.0, D, kmin, kmax, scale, dec, term, lo, hi);
      }
      if (!loc.empty()) comps.push_back({std::move(loc), (double)w});
    }
    for (auto& cp : comps) wsum += cp.second;
    for (auto& cp : comps) for (auto& kv : cp.first) m[kv.first] += kv.second * cp.second / wsum;
    s.c.reserve(m.size());
    for (auto& kv : m) s.c.push_back({kv.first, kv.second});
  }
  if (!f) { fprintf(stderr, "slots file truncated\n"); exit(1); }
  return S;
}

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

static int qp(double p1) {  // 0: p1 == 0, 13: p1 == 1, 1..12 in stretch domain
  if (p1 <= 0) return 0;
  if (p1 >= 1) return 13;
  double st = stretch(clampp(p1, 1e-5));
  int q = (int)std::floor((st + 9) / 1.5) + 1;
  return q < 1 ? 1 : q > 12 ? 12 : q;
}

struct Acc { double rec = 0, base = 0, full = 0; long bytes = 0, slots = 0; };

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: e12_sim COSTLOG SLOTS.bin [k=v ...]\n"); return 1; }
  for (int i = 3; i < argc; i++) { const char* e = strchr(argv[i], '='); if (e) P[std::string(argv[i], (size_t)(e - argv[i]))] = atof(e + 1); }
  const double lr = par("lr", 0.002), eps = par("eps", 1e-4);
  const int smlim = (int)par("smlim", 1023), direct = (int)par("direct", 1), clsmask = (int)par("clsmask", -1);
  const char* tsv = getenv("E12_TSV");  // per-slot rows: class, D, rec bits, base bits, full bits

  std::ifstream f(argv[1], std::ios::binary);
  f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(16);
  size_t n = (sz - 16) / 40;
  std::vector<Rec> R(n);
  f.read((char*)R.data(), n * 40);
  std::vector<Slot> S = load_slots(argv[2]);
  // per coded byte: slot index (or -1) and position in slot
  std::vector<int32_t> sidx(n, -1); std::vector<uint8_t> spos(n, 0);
  long overlaps = 0, ncand = 0;
  for (size_t k = 0; k < S.size(); k++) {
    if (clsmask >= 0 && !((clsmask >> S[k].cls) & 1)) continue;
    ncand += S[k].c.size();
    for (size_t j = 0; j < S[k].actual.size(); j++) {
      size_t i = S[k].start + j;
      if (i >= n) break;
      if (sidx[i] >= 0) { overlaps++; continue; }
      if ((uint8_t)R[i].byte != (uint8_t)S[k].actual[j]) { fprintf(stderr, "slot %zu byte mismatch\n", k); return 1; }
      sidx[i] = (int32_t)k; spos[i] = (uint8_t)j;
    }
  }
  fprintf(stderr, "%zu coded bytes, %zu slots, %ld candidates, %ld overlapping bytes\n", n, S.size(), ncand, overlaps);

  const int NPB = 4;  // byte position in slot: 0, 1, 2-3, 4+
  StateMap sm((size_t)NC * 14 * NPB * 8, smlim);
  const int NSETS = 1 + NC * 4 * 8;
  const int NIN = 4;
  Mixer mx(NIN, NSETS, lr), mb(2, 1 + NC * 8, lr);
  std::vector<Acc> bycls(NC);
  std::map<std::pair<int, int>, Acc> bycd;  // (class, log2 D bucket)
  Acc all, inslot;
  FILE* tf = tsv ? fopen(tsv, "w") : nullptr;
  if (tf) fprintf(tf, "slot\tclass\tD\tbytes\trec\tbase\tfull\n");
  // per-slot accumulators
  int cur = -1; size_t lo = 0, hi = 0; double s_rec = 0, s_base = 0, s_full = 0; bool alive = false;
  std::string pfx;
  auto flush = [&]() {
    if (cur < 0) return;
    Slot& s = S[cur];
    int db = s.D > 0 ? std::min(24, (int)std::log2((double)s.D)) : 0;
    for (Acc* a : {&bycls[s.cls], &bycd[{s.cls, db}]}) { a->rec += s_rec; a->base += s_base; a->full += s_full; a->slots++; a->bytes += s.actual.size(); }
    if (tf) fprintf(tf, "%d\t%s\t%d\t%zu\t%.3f\t%.3f\t%.3f\n", cur, CLASSES[s.cls], s.D, s.actual.size(), s_rec, s_base, s_full);
    cur = -1;
  };
  for (size_t i = 0; i < n; i++) {
    const Rec& r = R[i];
    int k = sidx[i];
    if (k >= 0 && spos[i] == 0) {
      flush();
      cur = k; lo = 0; hi = S[k].c.size(); s_rec = s_base = s_full = 0; alive = true;
      pfx.clear();
    }
    if (k < 0 && cur >= 0) flush();
    double brec = 0, bbase = 0, bfull = 0;
    int c = 0;  // partial byte (bits so far, with leading 1)
    for (int b = 0; b < 8; b++) {
      int y = (r.byte >> (7 - b)) & 1;
      double pr = clampp(r.pbit[b] / 65536.0);
      double xs[NIN] = {stretch(pr), 0.3, 0, 0};
      int set = 0, bset = 0, smi = -1;
      double p1 = -1;
      if (k >= 0) {
        Slot& s = S[k];
        bset = 1 + s.cls * 8 + b;
        int st = 3;  // abstain
        if (alive) {
          // candidates in [lo, hi) all share pfx (the coded bytes of the slot so far); next byte's partial bits = c
          size_t j = spos[i];
          double m0 = 0, m1 = 0;
          for (size_t t = lo; t < hi; t++) {
            const std::string& cs = s.c[t].s;
            if (cs.size() <= j) continue;
            int cb = (uint8_t)cs[j];
            if (b > 0 && (cb >> (8 - b)) != c) continue;
            if ((cb >> (7 - b)) & 1) m1 += s.c[t].w; else m0 += s.c[t].w;
          }
          if (m0 + m1 > 0) {
            p1 = m1 / (m0 + m1);
            st = m0 == 0 ? 1 : m1 == 0 ? 0 : 2;
            int pb = j == 0 ? 0 : j == 1 ? 1 : j < 4 ? 2 : 3;
            smi = ((s.cls * 14 + qp(p1)) * NPB + pb) * 8 + b;
            xs[2] = stretch(clampp(sm.p[smi], 1e-6));
            xs[3] = direct ? stretch(clampp(p1, eps)) : 0;
          } else alive = false;
        }
        set = 1 + (s.cls * 4 + st) * 8 + b;
      }
      double xb[2] = {xs[0], xs[1]};
      double pf = pr, pb_ = pr;  // outside slots both systems are the record
      if (k >= 0) { pf = clampp(squash(mx.dot(set, xs))); pb_ = clampp(squash(mb.dot(bset, xb))); }
      brec += -std::log2(y ? pr : 1 - pr);
      bbase += -std::log2(y ? pb_ : 1 - pb_);
      bfull += -std::log2(y ? pf : 1 - pf);
      if (k >= 0) { mx.upd(set, xs, y - pf); mb.upd(bset, xb, y - pb_); }
      if (smi >= 0) sm.upd(smi, y);
      c = (c << 1) | y;
      (void)p1;
    }
    if (k >= 0 && alive) {
      // narrow the candidate range to those whose byte j equals the coded byte
      Slot& s = S[k]; size_t j = spos[i];
      size_t nlo = hi, nhi = lo;
      for (size_t t = lo; t < hi; t++) {
        const std::string& cs = s.c[t].s;
        if (cs.size() > j && (uint8_t)cs[j] == r.byte) { nlo = std::min(nlo, t); nhi = std::max(nhi, t + 1); }
      }
      if (nlo >= nhi) alive = false; else { lo = nlo; hi = nhi; }
    }
    all.rec += brec; all.base += bbase; all.full += bfull; all.bytes++;
    if (k >= 0) { inslot.rec += brec; inslot.base += bbase; inslot.full += bfull; inslot.bytes++; s_rec += brec; s_base += bbase; s_full += bfull; }
  }
  flush();
  if (tf) fclose(tf);
  printf("all bytes: %ld  record %.0f B  baseline %.0f B  full %.0f B  gain(base-full) %+.0f B\n", all.bytes, all.rec / 8,
         all.base / 8, all.full / 8, (all.base - all.full) / 8);
  printf("in slots:  %ld bytes  record %.0f B  baseline %.0f B  full %.0f B  gain %+.0f B\n", inslot.bytes, inslot.rec / 8,
         inslot.base / 8, inslot.full / 8, (inslot.base - inslot.full) / 8);
  printf("%-11s %7s %7s %10s %10s %10s %9s %8s %8s\n", "class", "slots", "bytes", "rec B", "base B", "full B", "gain B",
         "rec b/sl", "full b/sl");
  for (int c = 0; c < NC; c++) {
    Acc& a = bycls[c];
    if (!a.slots) continue;
    printf("%-11s %7ld %7ld %10.0f %10.0f %10.0f %+9.0f %8.2f %8.2f\n", CLASSES[c], a.slots, a.bytes, a.rec / 8, a.base / 8,
           a.full / 8, (a.base - a.full) / 8, a.rec / a.slots, a.full / a.slots);
  }
  if (par("bycd", 0)) {
    printf("\nclass\tdbucket\tslots\trec_bits\tbase_bits\tfull_bits\n");
    for (auto& kv : bycd)
      printf("%s\t%d\t%ld\t%.1f\t%.1f\t%.1f\n", CLASSES[kv.first.first], kv.first.second, kv.second.slots, kv.second.rec,
             kv.second.base, kv.second.full);
  }
  return 0;
}
