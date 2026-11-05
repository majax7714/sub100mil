// A2: offline link-target model on top of the record's per-byte costs.
//
//   g++ -O2 -march=x86-64-v3 -o a2_sim scripts/analysis/a2_sim.cpp
//   ./a2_sim COSTLOG SPANS.tsv [key=value ...]      (SPANS from scripts/analysis/a2_extract.py)
//
// Everything is causal: a span's sequence enters the candidate index only after its last byte is coded.
// For every coded byte of a link target (after '[' up to and including the terminator '|' or ']'):
//   * experts: prefix tries over the CODED (WRT) byte sequences of earlier link targets / titles; each gives
//     counts of the next byte among the candidates that extend what has been typed so far:
//       0 LINK   all earlier link targets, frequency counts (terminator included)
//       1 LINKR  same, recency-decayed counts (time = links coded, tau = `tau`)
//       2 TITLE  earlier article titles (+ a virtual end that splits into ']' / '|' by global counts)
//       3 ART    links earlier in the same article
//       4 FOLD   links + titles with the leading capital flag removed (first-letter case folding)
//   * per-bit simulation (the realistic form): for each of the 8 bits, every expert's (n0, n1) mass on
//     the byte path so far goes through an adaptive StateMap keyed by (expert, q(n0), q(n1), typed);
//     a logistic mixer with inputs stretch(p_record_bit), bias, and the expert outputs, weight set
//     selected by (typed bucket, bit position, LINK has an opinion), mixes them. Baseline = the same mixer
//     with only stretch(p_record_bit) + bias, which isolates the information the experts add.
//   * byte-level linear cross-check: p = lam * p_rec(byte) + (1 - lam) * q(byte), q = LINK + TITLE counts,
//     lam = sigmoid(theta[ctx]) learned online by gradient on -log2 p.
// The record's p for the ACTUAL byte path is exactly what we need for both: pbit gives P(bit=1) on the
// path, and p_rec(byte) = 2^-cost.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
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
static inline double clampp(double p) { return p < 1e-6 ? 1e-6 : (p > 1 - 1e-6 ? 1 - 1e-6 : p); }

// ------------------------------------------------------------------ prefix trie with counts
struct Child { uint16_t b; uint32_t node; float cnt; float rec; int32_t last; };
struct Trie {
  std::vector<std::vector<Child>> nodes;
  double tau;
  Trie(double t = 1e9) : tau(t) { nodes.emplace_back(); }
  void clear() { nodes.clear(); nodes.emplace_back(); }
  int child(int n, int b) const {
    if (n < 0) return -1;
    for (auto& c : nodes[n]) if (c.b == b) return (int)c.node;
    return -1;
  }
  void insert(const std::vector<int>& s, int t) {
    int n = 0;
    for (int b : s) {
      Child* f = nullptr;
      for (auto& c : nodes[n]) if (c.b == b) { f = &c; break; }
      if (!f) {
        nodes[n].push_back({(uint16_t)b, (uint32_t)nodes.size(), 0, 0, t});
        nodes.emplace_back();
        f = &nodes[n].back();
      }
      f->cnt += 1;
      f->rec = f->rec * std::exp(-(t - f->last) / tau) + 1;
      f->last = t;
      n = f->node;
    }
  }
  size_t edges() const { size_t e = 0; for (auto& v : nodes) e += v.size(); return e; }
};

// distribution over next coded byte: mass[256]; END (256) is split by global terminator counts
struct Dist { double m[256]; double tot; };
static double term_cnt[256];
static double term_tot = 0;
static void dist_of(const Trie& T, int n, bool recency, int t, Dist& d) {
  std::memset(d.m, 0, sizeof d.m);
  d.tot = 0;
  if (n < 0) return;
  for (auto& c : T.nodes[n]) {
    double v = recency ? c.rec * std::exp(-(t - c.last) / T.tau) : c.cnt;
    if (c.b == 256) {
      for (int k = 0; k < 256; k++) if (term_cnt[k] > 0) d.m[k] += v * term_cnt[k] / term_tot;
    } else {
      d.m[c.b] += v;
    }
    d.tot += v;
  }
}

static int qn(double n) {
  static const double th[] = {0.05, 1.05, 2.05, 3.05, 4.05, 6.05, 9.05, 14.05, 24.05, 49.05};
  int q = 0;
  while (q < 10 && n >= th[q]) q++;
  return q;  // 0..10
}
static int typed_bucket(int j) { return j == 0 ? 0 : j == 1 ? 1 : j == 2 ? 2 : j < 6 ? 3 : 4; }

struct StateMap {
  std::vector<float> p; std::vector<uint16_t> n; int limit;
  StateMap(size_t sz, int lim) : p(sz, 0.5f), n(sz, 0), limit(lim) {}
  double get(size_t i) const { return p[i]; }
  void upd(size_t i, int y) { if (n[i] < limit) n[i]++; p[i] += (y - p[i]) / (n[i] + 0.5); }
};

struct Mixer {
  int nin; double lr; std::vector<double> w;
  Mixer(int ni, int nsets, double l) : nin(ni), lr(l), w((size_t)ni * nsets, 0.0) {
    for (int s = 0; s < nsets; s++) w[(size_t)s * ni] = 1.0;  // input 0 = record
  }
  double dot(int s, const double* x) const { double d = 0; for (int i = 0; i < nin; i++) d += w[(size_t)s * nin + i] * x[i]; return d; }
  void upd(int s, const double* x, double err) { for (int i = 0; i < nin; i++) w[(size_t)s * nin + i] += lr * err * x[i]; }
};

// One coded byte through "record + experts" (full) and "record only" (base) per-bit mixers; returns bits.
struct BitSim {
  int ne; StateMap sm; Mixer mx, mb;
  BitSim(int n, int smlim, double lr) : ne(n), sm((size_t)n * 11 * 11 * 5, smlim), mx(2 + n, 5 * 8 * 2 * 3, lr), mb(2, 5 * 8 * 2 * 3, lr) {}
  void code(const Rec& r, const Dist* D, int tb, double& cr, double& cb, double& cf) {
    cr = cb = cf = 0;
    int y8 = r.byte;
    for (int k = 0; k < 8; k++) {
      int bit = (y8 >> (7 - k)) & 1, hi = y8 >> (8 - k);
      double pr = clampp(r.pbit[k] / 65536.0);
      cr += -std::log2(bit ? pr : 1 - pr);
      double x[16]; size_t smi[14];
      x[0] = stretch(pr); x[1] = 0.3;
      bool has0 = false;
      for (int e = 0; e < ne; e++) {
        double n0 = 0, n1 = 0;
        int lo = hi << (8 - k), span = 1 << (8 - k), half = span >> 1;
        for (int b = lo; b < lo + half; b++) n0 += D[e].m[b];
        for (int b = lo + half; b < lo + span; b++) n1 += D[e].m[b];
        if (n0 + n1 < 1e-9) { x[2 + e] = 0; smi[e] = SIZE_MAX; continue; }
        if (e == 0) has0 = true;
        smi[e] = (((size_t)e * 11 + qn(n0)) * 11 + qn(n1)) * 5 + tb;
        x[2 + e] = stretch(clampp(sm.get(smi[e])));
      }
      double ast = std::fabs(x[0]);
      int conf = ast < 1.5 ? 0 : ast < 4 ? 1 : 2;
      int set = ((tb * 8 + k) * 2 + has0) * 3 + conf;
      double pf = clampp(squash(mx.dot(set, x))), pb = clampp(squash(mb.dot(set, x)));
      cf += -std::log2(bit ? pf : 1 - pf);
      cb += -std::log2(bit ? pb : 1 - pb);
      mx.upd(set, x, bit - pf); mb.upd(set, x, bit - pb);
      for (int e = 0; e < ne; e++) if (smi[e] != SIZE_MAX) sm.upd(smi[e], bit);
    }
  }
};

struct Span { char kind; int cs, ce, page, a1; std::string text; };

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: a2_sim COSTLOG SPANS.tsv [k=v ...]\n"); return 1; }
  for (int i = 3; i < argc; i++) { const char* e = strchr(argv[i], '='); if (e) P[std::string(argv[i], (size_t)(e - argv[i]))] = atof(e + 1); }
  const double lr = par("lr", 0.002), tau = par("tau", 2000), lam_lr = par("lamlr", 0.02);
  const int smlim = (int)par("smlim", 255);
  const int use_mask = (int)par("experts", 31);  // bitmask of experts used
  const char* dump = getenv("A2_DUMP");

  std::ifstream f(argv[1], std::ios::binary);
  f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(16);
  size_t n = (sz - 16) / 40;
  std::vector<Rec> R(n);
  f.read((char*)R.data(), n * 40);

  std::vector<Span> spans;
  {
    std::ifstream g(argv[2]); std::string line;
    while (std::getline(g, line)) {
      std::istringstream ss(line); Span s; std::string k;
      std::getline(ss, k, '\t'); s.kind = k[0];
      ss >> s.cs >> s.ce >> s.page >> s.a1; ss.get(); std::getline(ss, s.text);
      spans.push_back(s);
    }
  }
  // the raw coded value of the capital flag (decoded 0x40) is the SWAP image of 0x40: 0x40 is not remapped
  const int CAP = 0x40;

  const int NE = 5;
  Trie link(tau), title, art, fold;
  // StateMaps: expert x q(n0) x q(n1) x typed bucket
  StateMap sm((size_t)NE * 11 * 11 * 5, smlim);
  const int NSETS = 5 * 8 * 2 * 3 * 2;
  const int sel = (int)par("sel", 1);
  Mixer mx(2 + NE, NSETS, lr), mb(2, NSETS, lr);
  std::vector<double> theta(5 * 11 * 4, 2.0);  // linear byte mix: lam = sigmoid(theta), start at 0.88

  // per-link outcome accounting
  struct Acc { double rec = 0, base = 0, full = 0, lin = 0; long bytes = 0, links = 0; };
  std::map<std::string, Acc> acc;
  auto add = [&](const std::string& k, double r, double b, double fu, double li, long nb, long nl) {
    Acc& a = acc[k]; a.rec += r; a.base += b; a.full += fu; a.lin += li; a.bytes += nb; a.links += nl;
  };
  // seen status (folded coded sequence without terminator)
  std::unordered_map<std::string, int> seen_link, seen_title, seen_art;
  int art_page = -1;
  auto fold_key = [&](const std::vector<int>& s) {
    std::string k; size_t i0 = (!s.empty() && s[0] == CAP) ? 1 : 0;
    for (size_t i = i0; i < s.size(); i++) k.push_back((char)s[i]);
    return k;
  };

  // anchors (text after '|'): AT = the link's own target (first-letter folded) + ']', AH = earlier anchors of the
  // same target (folded), AG = earlier anchors of any target (folded)
  BitSim anc(3, smlim, lr);
  std::unordered_map<std::string, Trie> anc_hist;
  Trie anc_all;
  std::vector<int> last_body; std::string last_fk;
  const int CLOSE = 0x5D;
  auto fold_seq = [&](const std::vector<int>& v) {
    std::vector<int> o; size_t i0 = (!v.empty() && v[0] == CAP) ? 1 : 0;
    for (size_t i = i0; i < v.size(); i++) o.push_back(v[i]);
    return o;
  };
  FILE* df = dump ? fopen(dump, "w") : nullptr;
  int tlinks = 0, cur_page = -1;
  long total_link_bytes = 0;
  for (auto& s : spans) {
    std::vector<int> seq;
    for (int i = s.cs; i < s.ce; i++) seq.push_back(R[i].byte);
    if (s.page != cur_page) { art.clear(); cur_page = s.page; }
    if (s.kind == 'A') {
      Trie tgt; std::vector<int> tq = fold_seq(last_body); tq.push_back(CLOSE); tgt.insert(tq, 0);
      Trie& hist = anc_hist[last_fk];
      bool had = hist.nodes.size() > 1;
      int nT2 = 0, nH = 0, nG = 0;
      double lr_ = 0, lb_ = 0, lf_ = 0;
      Dist D[3];
      for (int j = 0; j < (int)seq.size(); j++) {
        const Rec& r = R[s.cs + j];
        int y8 = r.byte;
        dist_of(tgt, nT2, false, 0, D[0]);
        dist_of(hist, nH, false, 0, D[1]);
        dist_of(anc_all, nG, false, 0, D[2]);
        double cr, cb, cf;
        anc.code(r, D, typed_bucket(j), cr, cb, cf);
        lr_ += cr; lb_ += cb; lf_ += cf;
        if (!(j == 0 && y8 == CAP)) { nT2 = tgt.child(nT2, y8); nH = hist.child(nH, y8); nG = anc_all.child(nG, y8); }
      }
      bool same = fold_seq(std::vector<int>(seq.begin(), seq.end() - 1)) == fold_seq(last_body);
      add(std::string("anchor: ALL"), lr_, lb_, lf_, lr_, seq.size(), 1);
      add(std::string("anchor: target had earlier anchors ") + (had ? "yes" : "no"), lr_, lb_, lf_, lr_, seq.size(), 1);
      add(std::string("anchor: equals target (folded) ") + (same ? "yes" : "no"), lr_, lb_, lf_, lr_, seq.size(), 1);
      std::vector<int> aq = fold_seq(seq);
      hist.insert(aq, 0); anc_all.insert(aq, 0);
      continue;
    }
    if (s.kind == 'T') {
      std::vector<int> t2 = seq; t2.push_back(256);
      title.insert(t2, tlinks);
      std::vector<int> t3; size_t i0 = (!seq.empty() && seq[0] == CAP) ? 1 : 0;
      for (size_t i = i0; i < seq.size(); i++) t3.push_back(seq[i]);
      t3.push_back(256);
      fold.insert(t3, tlinks);
      seen_title[fold_key(seq)] = 1;
      continue;
    }
    // link target: seq includes the terminator
    std::vector<int> body(seq.begin(), seq.end() - 1);
    std::string fk = fold_key(body);
    const char* status = seen_link.count(fk) ? "seen as link" : seen_title.count(fk) ? "seen as title only" : "never seen";
    if (s.page != art_page) { seen_art.clear(); art_page = s.page; }
    int sc = seen_link.count(fk) ? seen_link[fk] : 0;
    const char* cb = sc == 0 ? "0" : sc == 1 ? "1" : sc < 5 ? "2-4" : sc < 20 ? "5-19" : sc < 100 ? "20-99" : "100+";
    const char* sa = seen_art.count(fk) ? "yes" : "no";
    double lrec = 0, lbase = 0, lfull = 0, llin = 0, frec = 0, ffull = 0, fbase = 0;
    int nL = 0, nT = 0, nA = 0, nF = 0;
    Dist D[NE];
    for (int j = 0; j < (int)seq.size(); j++) {
      const Rec& r = R[s.cs + j];
      int y8 = r.byte;
      int tb = typed_bucket(j);
      // expert distributions
      dist_of(link, nL, false, tlinks, D[0]);
      dist_of(link, nL, true, tlinks, D[1]);
      dist_of(title, nT, false, tlinks, D[2]);
      dist_of(art, nA, false, tlinks, D[3]);
      if (j == 0) { std::memset(D[4].m, 0, sizeof D[4].m); D[4].tot = 0; }
      else dist_of(fold, nF, false, tlinks, D[4]);
      for (int e = 0; e < NE; e++) if (!((use_mask >> e) & 1)) { std::memset(D[e].m, 0, sizeof D[e].m); D[e].tot = 0; }
      // ---- per-bit logistic simulation
      double cb = 0, cf = 0, cr = 0;
      for (int k = 0; k < 8; k++) {
        int bit = (y8 >> (7 - k)) & 1;
        int hi = y8 >> (8 - k);  // known top k bits
        double pr = clampp(r.pbit[k] / 65536.0);
        cr += -std::log2(bit ? pr : 1 - pr);
        double x[2 + NE];
        x[0] = stretch(pr); x[1] = 0.3;
        size_t smi[NE];
        bool has_link = false, has_art = false;
        for (int e = 0; e < NE; e++) {
          double n0 = 0, n1 = 0;
          int lo = hi << (8 - k), span = 1 << (8 - k), half = span >> 1;
          for (int b = lo; b < lo + half; b++) n0 += D[e].m[b];
          for (int b = lo + half; b < lo + span; b++) n1 += D[e].m[b];
          if (n0 + n1 < 1e-9) { x[2 + e] = 0; smi[e] = SIZE_MAX; continue; }
          if (e == 0) has_link = true;
          if (e == 3) has_art = true;
          smi[e] = (((size_t)e * 11 + qn(n0)) * 11 + qn(n1)) * 5 + tb;
          x[2 + e] = stretch(clampp(sm.get(smi[e])));
        }
        double ast = std::fabs(x[0]);
        int conf = sel >= 1 ? (ast < 1.5 ? 0 : ast < 4 ? 1 : 2) : 0;
        int set = (((tb * 8 + k) * 2 + has_link) * 3 + conf) * 2 + (sel >= 2 ? has_art : 0);
        double pf = clampp(squash(mx.dot(set, x)));
        double pb = clampp(squash(mb.dot(set, x)));
        cf += -std::log2(bit ? pf : 1 - pf);
        cb += -std::log2(bit ? pb : 1 - pb);
        mx.upd(set, x, bit - pf);
        mb.upd(set, x, bit - pb);
        for (int e = 0; e < NE; e++) if (smi[e] != SIZE_MAX) sm.upd(smi[e], bit);
      }
      // ---- byte-level linear mix (LINK + TITLE counts)
      double qt = D[0].tot + D[2].tot, cl = cr;
      if (qt > 0) {
        double q = (D[0].m[y8] + D[2].m[y8]) / qt, top = 0;
        for (int b = 0; b < 256; b++) top = std::max(top, D[0].m[b] + D[2].m[b]);
        int share = top / qt > 0.9 ? 3 : top / qt > 0.6 ? 2 : top / qt > 0.3 ? 1 : 0;
        int ci = (tb * 11 + qn(qt)) * 4 + share;
        double lam = squash(theta[ci]), a = std::exp2(-cr);
        double p = std::max(lam * a + (1 - lam) * q, 1e-12);
        cl = -std::log2(p);
        theta[ci] -= lam_lr * (-(a - q) * lam * (1 - lam) / p);
      }
      lrec += cr; lbase += cb; lfull += cf; llin += cl;
      if (j == 0) { frec = cr; fbase = cb; ffull = cf; }
      if (df) fprintf(df, "%d\t%d\t%d\t%.4f\t%.4f\t%.4f\t%.4f\n", s.cs + j, j, (int)(j == (int)seq.size() - 1), cr, cb, cf, cl);
      // advance the tries along the actual byte
      nL = link.child(nL, y8); nT = title.child(nT, y8); nA = art.child(nA, y8);
      if (!(j == 0 && y8 == CAP)) nF = fold.child(nF, y8);
    }
    total_link_bytes += seq.size();
    std::string cls = s.a1 ? "A1 link-target class" : "other (in tmpl/ref/table/redirect)";
    add(std::string("status: ") + status, lrec, lbase, lfull, llin, seq.size(), 1);
    add("class: " + cls, lrec, lbase, lfull, llin, seq.size(), 1);
    add("ALL", lrec, lbase, lfull, llin, seq.size(), 1);
    add(std::string("seen count ") + cb, lrec, lbase, lfull, llin, seq.size(), 1);
    if (sc) add(std::string("seen, same article ") + sa, lrec, lbase, lfull, llin, seq.size(), 1);
    add(std::string("half ") + (s.cs < (int)(n / 2) ? "1" : "2") + (sc ? " seen" : " never"), lrec, lbase, lfull, llin, seq.size(), 1);
    add("pos: first byte", frec, fbase, ffull, frec, 1, 0);
    {
      const Rec& r = R[s.ce - 1];
      (void)r;
    }
    // index the link (causal: after its last byte)
    link.insert(seq, tlinks);
    art.insert(seq, tlinks);
    std::vector<int> t3; size_t i0 = (!body.empty() && body[0] == CAP) ? 1 : 0;
    for (size_t i = i0; i < seq.size(); i++) t3.push_back(seq[i]);
    fold.insert(t3, tlinks);
    term_cnt[seq.back()] += 1; term_tot += 1;
    seen_link[fk] += 1;
    last_body = body; last_fk = fk;
    seen_art[fk] = 1;
    tlinks++;
  }
  if (df) fclose(df);
  double tot = 0; for (auto& r : R) tot += r.cost;
  printf("coded bytes %zu, record total %.0f B; links %d; link-target bytes (after '[' incl. terminator) %ld\n",
         n, tot / 8, tlinks, total_link_bytes);
  printf("params: lr=%g tau=%g lamlr=%g smlim=%d experts=%d\n", lr, tau, lam_lr, smlim, use_mask);
  printf("trie sizes (nodes/edges): link %zu/%zu title %zu/%zu fold %zu/%zu\n", link.nodes.size(), link.edges(),
         title.nodes.size(), title.edges(), fold.nodes.size(), fold.edges());
  printf("%-44s %8s %9s %11s %11s %11s %11s %9s %9s %8s\n", "subset", "links", "bytes", "record B", "base B", "A2 bit B",
         "A2 lin B", "gain bit", "gain lin", "%subset");
  for (auto& [k, a] : acc) {
    printf("%-44s %8ld %9ld %11.0f %11.0f %11.0f %11.0f %9.0f %9.0f %7.2f%%\n", k.c_str(), a.links, a.bytes, a.rec / 8,
           a.base / 8, a.full / 8, a.lin / 8, (a.base - a.full) / 8, (a.rec - a.lin) / 8, 100 * (a.base - a.full) / a.rec);
  }
  return 0;
}
