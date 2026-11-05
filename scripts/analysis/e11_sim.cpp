// E11: offline in-article cache model on top of the record's per-byte costs.
//
//   g++ -O2 -march=x86-64-v3 -o build/exp/E11/e11_sim scripts/analysis/e11_sim.cpp
//   build/exp/E11/e11_sim COSTLOG AUX.bin [key=value ...]          (AUX from scripts/analysis/e11_extract.py)
//
// Tokens live in the CODED (WRT) stream: a token is a maximal run of "word bytes" (logical value a-z, A-Z, 0-9,
// >= 0x80 i.e. dictionary-code bytes, the case flags 0x40/0x07/0x06, and the escape 0x0C plus its escaped byte).
// A dictionary word is one to three code bytes, so the cache works on the alphabet the coder codes.
// The sequence a cache stores for a token is its raw coded bytes plus the raw byte that ended it (the terminator),
// so the cache also predicts where the token stops.
//
// Experts (bitmask `experts`, default all). "A" = in-article (reset at every <page>), "G" = whole file (never reset):
//   0 AU   in-article unigram: frequency counts of tokens used earlier in this article
//   1 AUR  in-article unigram, recency-decayed (time = tokens, tau = `taua`)
//   2 AB1  in-article bigram: tokens that followed the previous token earlier in this article
//   3 AB2  in-article trigram: same with the previous two tokens
//   4 ARU  in-article unigram over RARE tokens only: tokens that occurred in at most `rthr` earlier articles
//   5 GUR  whole-file unigram, recency-decayed (tau = `taug` tokens)
//   6 GB1  whole-file bigram counts
//   7 GB2  whole-file trigram counts
// At a token boundary (previous byte not a word byte) an expert predicts the first byte of the next token from its
// root; inside a token it follows the typed prefix (silent once the prefix leaves its trie).
//
// Per-bit simulation (A2's method): for each of the 8 bits, each expert's mass (n0, n1) on the two halves of the
// remaining byte range goes through an adaptive StateMap keyed by (expert, q(n0), q(n1), position bucket); a
// logistic mixer with inputs stretch(p_record_bit), a bias and the expert outputs mixes them, weight set selected
// by (position bucket, bit, record confidence, any A expert has an opinion, any G expert has an opinion).
// Baseline = the same mixer with only stretch(p_record_bit) + bias and the same weight-set selection, so neither
// recalibration nor the selector is credited to the experts. Everything is causal: a token enters the caches only
// after its terminator is coded; the analysis labels (actual token rarity, in-article repeat count) are used
// only for accounting.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

static const char* CLASSES[] = {
    "prose", "html/entity", "heading", "link anchor", "link target", "image/file", "external link/URL",
    "category", "template", "table", "infobox", "ref/citation", "math",
    "redirect page", "page XML (tags)", "title", "edit comment", "metadata block (ids/time/user)",
    "interlanguage block", "header/intro/coda", "container (block hdr)",
};
static const int NCLS = 21;

// SWAP as in a1_analyze.py: raw coded byte -> logical value
static int SWAP[256];
static void init_swap() {
  for (int c = 0; c < 256; c++) {
    int d = c;
    if ('{' <= d && d < 127) d += 'P' - '{';
    else if ('P' <= d && d < 'T') d -= 'P' - '{';
    else if ((':' <= d && d <= '?') || ('J' <= d && d <= 'O')) d ^= 0x70;
    if (d == 'X' || d == '`') d ^= 'X' ^ '`';
    SWAP[c] = d;
  }
}

// ------------------------------------------------------------------ context tries (shared node pool)
struct Child { uint16_t b; uint32_t node; float cnt; float rec; int32_t last; float wr; };
struct CTrie {
  std::vector<std::vector<Child>> nodes;
  std::unordered_map<uint64_t, uint32_t> roots;
  size_t peak_nodes = 0;
  void clear() {
    peak_nodes = std::max(peak_nodes, nodes.size());
    nodes.clear(); roots.clear();
  }
  int root(uint64_t ctx) const { auto it = roots.find(ctx); return it == roots.end() ? -1 : (int)it->second; }
  int child(int n, int b) const {
    if (n < 0) return -1;
    for (auto& c : nodes[n]) if (c.b == b) return (int)c.node;
    return -1;
  }
  void insert(uint64_t ctx, const std::vector<uint8_t>& s, int t, double tau, float w = 1) {
    auto it = roots.find(ctx);
    uint32_t n;
    if (it == roots.end()) { n = nodes.size(); nodes.emplace_back(); roots[ctx] = n; }
    else n = it->second;
    for (int b : s) {
      Child* f = nullptr;
      for (auto& c : nodes[n]) if (c.b == b) { f = &c; break; }
      if (!f) {
        uint32_t nn = nodes.size();
        nodes[n].push_back({(uint16_t)b, nn, 0, 0, t, 0});
        nodes.emplace_back();
        f = &nodes[n].back();
      }
      f->cnt += 1;
      f->wr += w;
      f->rec = f->rec * (float)std::exp(-(t - f->last) / tau) + 1;
      f->last = t;
      n = f->node;
    }
  }
  size_t mem() const {
    size_t m = nodes.capacity() * sizeof(std::vector<Child>) + roots.size() * 32;
    for (auto& v : nodes) m += v.capacity() * sizeof(Child);
    return m;
  }
};

static int qn(double n) {
  static const double th[] = {0.05, 1.05, 2.05, 3.05, 4.05, 6.05, 9.05, 14.05, 24.05, 49.05};
  int q = 0;
  while (q < 10 && n >= th[q]) q++;
  return q;  // 0..10
}

struct StateMap {
  std::vector<float> p; std::vector<uint16_t> n; int limit;
  StateMap(size_t sz, int lim) : p(sz, 0.5f), n(sz, 0), limit(lim) {}
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

static inline uint64_t hmix(uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2); h *= 0xff51afd7ed558ccdULL; return h ^ (h >> 29); }
static uint64_t hseq(const std::vector<uint8_t>& s, size_t n) { uint64_t h = 1469598103934665603ULL; for (size_t i = 0; i < n; i++) h = hmix(h, s[i]); return h; }

struct Acc { double rec = 0, base = 0, full = 0; long bytes = 0; };

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: e11_sim COSTLOG AUX.bin [k=v ...]\n"); return 1; }
  for (int i = 3; i < argc; i++) { const char* e = strchr(argv[i], '='); if (e) P[std::string(argv[i], (size_t)(e - argv[i]))] = atof(e + 1); }
  init_swap();
  const double lr = par("lr", 0.001), taua = par("taua", 100), taug = par("taug", 5000);
  const int smlim = (int)par("smlim", 255), mask = (int)par("experts", 255), rthr = (int)par("rthr", 2);
  const int nosel = (int)par("nosel", 0);
  const int smconf = (int)par("smconf", 0);   // 1: StateMaps also keyed by the record's confidence (3 levels)
  const int agree = (int)par("agree", 0);
  const int nobound = (int)par("nobound", 0);  // 1: experts silent at token boundaries (only inside a typed token)
  const int direct = (int)par("direct", 0);
  const int mix2 = (int)par("mix2", 0);  // 1: two-layer mixing, a bank of 3 differently selected mixers + a final mixer
  const double lr2 = par("lr2", 0.002);    // 1: each expert also feeds stretch((n1+.4)/(n0+n1+.8)) directly     // 1: weight set also selected by "strongest A expert agrees with the record"
  const double ralpha = par("ralpha", -1);    // >= 0: expert 4 = in-article unigram with mass sum 1/(1+df)^ralpha
                                              //  (instead of the df <= rthr filter)  // 1: weight-set selection without the has-opinion flags
  const char* dump = getenv("E11_DUMP");
  const char* gdfpath = getenv("E11_GDF");  // uint32 per token: global df in full enwik9 (e11_fullscale gdfout=)    // per-byte (record, base, full) bits as float32 triples

  std::ifstream f(argv[1], std::ios::binary);
  f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(16);
  size_t n = (sz - 16) / 40;
  std::vector<Rec> R(n);
  f.read((char*)R.data(), n * 40);
  std::vector<int32_t> aid(n); std::vector<uint8_t> cls(n);
  {
    std::ifstream g(argv[2], std::ios::binary);
    g.read((char*)aid.data(), n * 4); g.read((char*)cls.data(), n);
    if (!g) { fprintf(stderr, "aux file too short\n"); return 1; }
  }

  // word-byte flags (causal: depends on earlier bytes only through the escape state)
  std::vector<uint8_t> isw(n);
  {
    bool esc = false;
    for (size_t i = 0; i < n; i++) {
      int c = SWAP[R[i].byte];
      bool w;
      if (esc) { w = true; esc = false; }
      else {
        w = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80 || c == 0x40 ||
            c == 0x07 || c == 0x06 || c == 0x0C;
        if (c == 0x0C) esc = true;
      }
      isw[i] = w;
    }
  }

  // ---- analysis pre-pass (labels for accounting only): for each byte of a token (word bytes + its terminator):
  //   rarity bucket of the actual token = number of EARLIER articles that contained it; in-article repeat count.
  std::vector<uint8_t> lab_df(n, 255), lab_rep(n, 255), lab_gdf(n, 255);
  std::vector<uint32_t> gdfv;
  if (gdfpath) {
    std::ifstream g(gdfpath, std::ios::binary); uint32_t v;
    while (g.read((char*)&v, 4)) gdfv.push_back(v);
  }
  {
    size_t tokno = 0;  // tokens in e11_tokhash.py's order
    std::unordered_map<uint64_t, int> df;
    std::unordered_map<uint64_t, int> inart;
    int cur = INT32_MIN;
    size_t ts = SIZE_MAX;
    std::vector<uint8_t> tok;
    for (size_t i = 0; i < n; i++) {
      if (aid[i] != cur) {
        for (auto& kv : inart) df[kv.first]++;
        inart.clear(); cur = aid[i]; ts = SIZE_MAX; tok.clear();
      }
      if (isw[i]) { if (ts == SIZE_MAX) ts = i; tok.push_back(R[i].byte); continue; }
      if (ts != SIZE_MAX) {
        uint64_t h = hseq(tok, tok.size());
        int d = df.count(h) ? df[h] : 0;
        int r = inart.count(h) ? inart[h] : 0;
        uint8_t db = d == 0 ? 0 : d == 1 ? 1 : d < 5 ? 2 : d < 20 ? 3 : d < 100 ? 4 : 5;
        uint8_t rb = r == 0 ? 0 : r == 1 ? 1 : r < 5 ? 2 : 3;
        uint8_t gb = 255;
        if (tokno < gdfv.size()) { uint32_t g = gdfv[tokno]; gb = g < 2 ? 0 : g < 10 ? 1 : g < 100 ? 2 : g < 1000 ? 3 : g < 10000 ? 4 : 5; }
        tokno++;
        for (size_t j = ts; j <= i; j++) { lab_df[j] = db; lab_rep[j] = rb; lab_gdf[j] = gb; }
        inart[h]++;
        ts = SIZE_MAX; tok.clear();
      }
    }
  }

  const int NE = 8;
  const bool isA[NE] = {1, 1, 1, 1, 1, 0, 0, 0};
  CTrie A_uni, A_bi, A_tri, A_rare, G_uni, G_bi, G_tri;
  std::unordered_map<uint64_t, int> df;  // token hash -> number of earlier articles containing it
  std::unordered_set<uint64_t> inart;
  const int NPB = 6;
  StateMap sm((size_t)NE * 11 * 11 * NPB * 3, smlim);
  const int NSETS = NPB * 8 * 3 * 2 * 2 * 2;
  Mixer mx(2 + 2 * NE, NSETS, lr), mb(2, NSETS, lr);
  // mix2 bank: M1 = the set above; M2 = (bit, AU deterministic on this bit, q(AU mass)); M3 = (bit, position bucket,
  // mean rarity weight of the AU candidates on the path, 4 levels). Final: inputs stretch(p_M1..3) + stretch(p_rec),
  // selected by bit. The baseline has the same structure with record + bias inputs only.
  const int NS2 = 8 * 2 * 11, NS3 = 8 * NPB * 4;
  Mixer mx2(2 + 2 * NE, NS2, lr), mb2(2, NS2, lr), mx3(2 + 2 * NE, NS3, lr), mb3(2, NS3, lr);
  Mixer fx(4, 8, lr2), fb(4, 8, lr2);
  for (int k = 0; k < 8; k++) for (int j = 0; j < 4; j++) { fx.w[k * 4 + j] = fb.w[k * 4 + j] = j < 3 ? 1.0 / 3 : 0; }

  std::vector<Acc> by_cls(NCLS), by_pb(NPB), by_df(7), by_rep(5), by_off(6), by_half(2);
  std::map<std::string, Acc> by_misc;
  // projection table, page bytes only: (in-article repeat bucket 0..3, df bucket 0..5) and separators (cell 24);
  // xtok counts tokens (terminator bytes)
  std::vector<Acc> xc(25); std::vector<long> xtok(25, 0); Acc nonpage;
  std::vector<Acc> xg(25), xgh[2] = {std::vector<Acc>(25), std::vector<Acc>(25)};
  std::vector<long> xgt(25, 0), xght[2] = {std::vector<long>(25, 0), std::vector<long>(25, 0)};
  std::vector<Acc> xh[2] = {std::vector<Acc>(25), std::vector<Acc>(25)}; std::vector<long> xht[2] = {std::vector<long>(25, 0), std::vector<long>(25, 0)};
  Acc all;
  // oracle (per token, AU / AUR caches "told for free" the token is in the cache): sum of record bits over the token's
  // bytes (first byte .. terminator) vs the cache's own byte-level cost -log2(m(actual)/sum m) along the path
  struct Orc { double rec = 0, cu = 0, cr = 0, minu = 0, minr = 0; long toks = 0; };
  std::map<std::string, Orc> orc;
  double tok_rec = 0, tok_cu = 0, tok_cr = 0; bool tok_inc = false;
  FILE* df_out = dump ? fopen(dump, "wb") : nullptr;

  int cur = INT32_MIN;
  int ttok = 0, atok = 0;           // global token time, tokens in this article
  size_t art_start = 0;
  std::vector<uint8_t> tok;         // raw bytes of the token being typed
  uint64_t prev1 = 0, prev2 = 0;    // hashes of the previous one/two tokens in this article (0 = none)
  uint64_t gprev1 = 0, gprev2 = 0;  // same, never reset
  int node[NE];
  struct Cand { int b; double m, m2; };
  std::vector<Cand> cand[NE];
  auto reset_nodes = [&]() {
    node[0] = A_uni.root(0); node[1] = node[0];
    node[2] = prev1 ? A_bi.root(prev1) : -1;
    node[3] = prev2 ? A_tri.root(hmix(prev2, prev1)) : -1;
    node[4] = A_rare.root(0);
    node[5] = G_uni.root(0);
    node[6] = gprev1 ? G_bi.root(gprev1) : -1;
    node[7] = gprev2 ? G_tri.root(hmix(gprev2, gprev1)) : -1;
  };
  auto trie_of = [&](int e) -> CTrie& {
    switch (e) { case 0: case 1: return A_uni; case 2: return A_bi; case 3: return A_tri; case 4: return A_rare;
      case 5: return G_uni; case 6: return G_bi; default: return G_tri; }
  };
  reset_nodes();
  bool in_tok = false;
  for (size_t i = 0; i < n; i++) {
    if (aid[i] != cur) {  // article boundary: reset in-article state
      for (auto h : inart) df[h]++;
      inart.clear();
      A_uni.clear(); A_bi.clear(); A_tri.clear(); A_rare.clear();
      cur = aid[i]; atok = 0; prev1 = prev2 = 0; tok.clear(); in_tok = false; art_start = i;
      reset_nodes();
    }
    const Rec& r = R[i];
    int y8 = r.byte;
    // position bucket
    int typed = (int)tok.size();
    int pb;
    if (!in_tok) pb = (i > 0 && SWAP[R[i - 1].byte] == ' ') ? 0 : 1;
    else pb = typed == 1 ? 2 : typed == 2 ? 3 : typed < 6 ? 4 : 5;
    // candidate lists
    for (int e = 0; e < NE; e++) {
      cand[e].clear();
      if (!((mask >> e) & 1) || node[e] < 0) continue;
      CTrie& T = trie_of(e);
      for (auto& c : T.nodes[node[e]]) {
        double m;
        if (e == 1) m = c.rec * std::exp(-(atok - c.last) / taua);
        else if (e == 5) m = c.rec * std::exp(-(ttok - c.last) / taug);
        else if (e == 4 && ralpha >= 0) m = c.wr;
        else m = c.cnt;
        cand[e].push_back({c.b, m, (double)c.wr});
      }
    }
    // oracle bookkeeping: byte-level cache probabilities of the actual byte (before the per-bit filtering)
    bool tstart = !in_tok && isw[i];
    if (tstart) { tok_rec = tok_cu = tok_cr = 0; tok_inc = true; }
    if (tok_inc) {
      for (int e = 0; e < 2; e++) {
        double tot = 0, my = 0;
        for (auto& c : cand[e]) { tot += c.m; if (c.b == y8) my = c.m; }
        if (my <= 0) tok_inc = false;
        else (e ? tok_cr : tok_cu) += -std::log2(my / tot);
      }
    }
    double cr = 0, cb = 0, cf = 0;
    for (int k = 0; k < 8; k++) {
      int bit = (y8 >> (7 - k)) & 1;
      double pr = clampp(r.pbit[k] / 65536.0);
      cr += -std::log2(bit ? pr : 1 - pr);
      double x[2 + 2 * NE];
      size_t smi[NE];
      x[0] = stretch(pr); x[1] = 0.3;
      bool hasA = false, hasG = false;
      double ast = std::fabs(x[0]);
      int conf = ast < 1.5 ? 0 : ast < 4 ? 1 : 2;
      for (int e = 0; e < NE; e++) {
        double n0 = 0, n1 = 0;
        for (auto& c : cand[e]) { if ((c.b >> (7 - k)) & 1) n1 += c.m; else n0 += c.m; }
        x[2 + NE + e] = 0;
        if (n0 + n1 < 1e-9 || (nobound && pb <= 1)) { x[2 + e] = 0; smi[e] = SIZE_MAX; continue; }
        if (direct) x[2 + NE + e] = stretch((n1 + 0.4) / (n0 + n1 + 0.8));
        (isA[e] ? hasA : hasG) = true;
        smi[e] = ((((size_t)e * 11 + qn(n0)) * 11 + qn(n1)) * NPB + pb) * 3 + (smconf ? conf : 0);
        x[2 + e] = stretch(clampp(sm.p[smi[e]]));
      }
      int agr = 0;
      if (agree) {
        double best = 0;
        for (int e = 0; e < NE; e++) if (isA[e] && std::fabs(x[2 + e]) > std::fabs(best)) best = x[2 + e];
        agr = best * x[0] > 0;
      }
      int set = ((((pb * 8 + k) * 3 + conf) * 2 + (nosel ? 0 : hasA)) * 2 + (nosel ? 0 : hasG)) * 2 + agr;
      double pf = clampp(squash(mx.dot(set, x))), pbs = clampp(squash(mb.dot(set, x)));
      if (mix2) {
        double a0 = 0, a1 = 0, w0 = 0;
        for (auto& c : cand[0]) { ((c.b >> (7 - k)) & 1 ? a1 : a0) += c.m; w0 += c.m2; }
        double at = a0 + a1;
        int det = at > 0 && (a0 == 0 || a1 == 0);
        double rw = at > 0 ? w0 / at : 1;
        int rq = at <= 0 ? 0 : rw > 0.5 ? 3 : rw > 0.1 ? 2 : 1;
        int s2 = (k * 2 + det) * 11 + qn(at), s3 = (k * NPB + pb) * 4 + rq;
        double p2 = clampp(squash(mx2.dot(s2, x))), q2 = clampp(squash(mb2.dot(s2, x)));
        double p3 = clampp(squash(mx3.dot(s3, x))), q3 = clampp(squash(mb3.dot(s3, x)));
        mx2.upd(s2, x, bit - p2); mb2.upd(s2, x, bit - q2); mx3.upd(s3, x, bit - p3); mb3.upd(s3, x, bit - q3);
        mx.upd(set, x, bit - pf); mb.upd(set, x, bit - pbs);
        double fxi[4] = {stretch(pf), stretch(p2), stretch(p3), x[0]}, fbi[4] = {stretch(pbs), stretch(q2), stretch(q3), x[0]};
        double pff = clampp(squash(fx.dot(k, fxi))), pfb = clampp(squash(fb.dot(k, fbi)));
        fx.upd(k, fxi, bit - pff); fb.upd(k, fbi, bit - pfb);
        pf = pff; pbs = pfb;
      } else {
        mx.upd(set, x, bit - pf); mb.upd(set, x, bit - pbs);
      }
      cf += -std::log2(bit ? pf : 1 - pf);
      cb += -std::log2(bit ? pbs : 1 - pbs);
      for (int e = 0; e < NE; e++) if (smi[e] != SIZE_MAX) sm.upd(smi[e], bit);
      // keep only candidates on the actual path
      for (int e = 0; e < NE; e++) {
        auto& v = cand[e]; size_t w = 0;
        for (auto& c : v) if (((c.b >> (7 - k)) & 1) == bit) v[w++] = c;
        v.resize(w);
      }
    }
    // accounting
    auto addto = [&](Acc& a) { a.rec += cr; a.base += cb; a.full += cf; a.bytes++; };
    addto(all); addto(by_cls[cls[i]]); addto(by_pb[pb]);
    addto(by_df[lab_df[i] == 255 ? 6 : lab_df[i]]); addto(by_rep[lab_rep[i] == 255 ? 4 : lab_rep[i]]);
    size_t off = i - art_start;
    addto(by_off[off < 1024 ? 0 : off < 4096 ? 1 : off < 16384 ? 2 : off < 65536 ? 3 : 4]);
    addto(by_half[i >= n / 2]);
    if (aid[i] >= 0) {
      int cell = lab_rep[i] == 255 ? 24 : lab_rep[i] * 6 + lab_df[i];
      addto(xc[cell]); addto(xh[i >= n / 2][cell]);
      if (gdfpath) {
        int gc = lab_rep[i] == 255 ? 24 : lab_gdf[i] == 255 ? 24 : lab_rep[i] * 6 + lab_gdf[i];
        addto(xg[gc]); addto(xgh[i >= n / 2][gc]);
        if (gc < 24 && !isw[i]) { xgt[gc]++; xght[i >= n / 2][gc]++; }
      }
      if (cell < 24 && !isw[i]) { xtok[cell]++; xht[i >= n / 2][cell]++; }
    } else addto(nonpage);
    if (cls[i] == 0) addto(by_misc[std::string("prose, token df ") + (lab_df[i] == 255 ? "sep" : std::to_string(lab_df[i])) +
                                   ", in-article repeat " + (lab_rep[i] == 255 ? "-" : std::to_string(lab_rep[i]))]);
    if (in_tok || tstart) tok_rec += cr;
    if ((in_tok || tstart) && !isw[i]) {  // token complete (this byte is its terminator)
      std::string kk = std::string("rep ") + std::to_string(lab_rep[i]) + " df " + std::to_string(lab_df[i]);
      for (const std::string& key : {kk, std::string("rep ") + std::to_string(lab_rep[i]), std::string("ALL")}) {
        Orc& o = orc[key];
        o.toks++;
        o.rec += tok_rec;
        if (tok_inc) { o.cu += tok_cu; o.cr += tok_cr; o.minu += std::min(tok_rec, tok_cu); o.minr += std::min(tok_rec, tok_cr); }
        else { o.cu += tok_rec; o.cr += tok_rec; o.minu += tok_rec; o.minr += tok_rec; }
      }
      tok_inc = false;
    }
    if (df_out) { float t3[3] = {(float)cr, (float)cb, (float)cf}; fwrite(t3, 4, 3, df_out); }

    // advance state with the actual byte
    if (isw[i]) {
      tok.push_back(y8); in_tok = true;
      for (int e = 0; e < NE; e++) node[e] = trie_of(e).child(node[e], y8);
    } else {
      if (in_tok) {  // token complete: index it (sequence = token bytes + terminator)
        uint64_t h = hseq(tok, tok.size());
        std::vector<uint8_t> s = tok; s.push_back(y8);
        A_uni.insert(0, s, atok, taua, (float)(1.0 / (1.0 + (df.count(h) ? df[h] : 0))));
        if (prev1) A_bi.insert(prev1, s, atok, 1e9);
        if (prev2) A_tri.insert(hmix(prev2, prev1), s, atok, 1e9);
        int d = df.count(h) ? df[h] : 0;
        if (ralpha >= 0) A_rare.insert(0, s, atok, 1e9, (float)std::pow(1.0 + d, -ralpha));
        else if (d <= rthr) A_rare.insert(0, s, atok, 1e9);
        if ((mask >> 5) & 1) G_uni.insert(0, s, ttok, taug);
        if (((mask >> 6) & 1) && gprev1) G_bi.insert(gprev1, s, ttok, 1e9);
        if (((mask >> 7) & 1) && gprev2) G_tri.insert(hmix(gprev2, gprev1), s, ttok, 1e9);
        inart.insert(h);
        prev2 = prev1; prev1 = h; gprev2 = gprev1; gprev1 = h;
        atok++; ttok++;
        tok.clear(); in_tok = false;
      }
      reset_nodes();
    }
  }
  if (df_out) fclose(df_out);

  printf("coded bytes %zu; tokens %d; params: lr=%g taua=%g taug=%g rthr=%d experts=%d nosel=%d smconf=%d agree=%d ralpha=%g\n", n,
         ttok, lr, taua, taug, rthr, mask, nosel, smconf, agree, ralpha);
  A_uni.clear(); A_bi.clear(); A_tri.clear(); A_rare.clear();
  printf("peak in-article trie nodes: uni %zu bi %zu tri %zu rare %zu; global nodes uni %zu bi %zu tri %zu (%.0f MB)\n",
         A_uni.peak_nodes, A_bi.peak_nodes, A_tri.peak_nodes, A_rare.peak_nodes, G_uni.nodes.size(), G_bi.nodes.size(),
         G_tri.nodes.size(), (G_uni.mem() + G_bi.mem() + G_tri.mem()) / 1e6);
  printf("TOTAL record %.0f B, baseline %.0f B, with experts %.0f B: GAIN %.0f B (%.4f%% of record)\n", all.rec / 8,
         all.base / 8, all.full / 8, (all.base - all.full) / 8, 100 * (all.base - all.full) / all.rec);
  auto row = [&](const std::string& name, const Acc& a) {
    if (!a.bytes) return;
    printf("  %-58s %9ld %10.0f %8.0f %7.3f%% %6.3f%%\n", name.c_str(), a.bytes, a.rec / 8, (a.base - a.full) / 8,
           100 * (a.base - a.full) / std::max(a.rec, 1e-9), 100 * (a.base - a.full) / all.rec);
  };
  printf("  %-58s %9s %10s %8s %8s %7s\n", "subset", "bytes", "record B", "gain B", "%subset", "%total");
  for (int c = 0; c < NCLS; c++) row(std::string("class: ") + CLASSES[c], by_cls[c]);
  const char* pbn[] = {"boundary after space", "boundary after other", "typed 1", "typed 2", "typed 3-5", "typed 6+"};
  for (int p = 0; p < NPB; p++) row(std::string("pos: ") + pbn[p], by_pb[p]);
  const char* dfn[] = {"0 (new to file)", "1", "2-4", "5-19", "20-99", "100+", "separator byte"};
  for (int p = 0; p < 7; p++) row(std::string("token in # earlier articles: ") + dfn[p], by_df[p]);
  const char* rpn[] = {"0", "1", "2-4", "5+", "separator byte"};
  for (int p = 0; p < 5; p++) row(std::string("token earlier in this article: ") + rpn[p], by_rep[p]);
  const char* ofn[] = {"<1K", "1K-4K", "4K-16K", "16K-64K", "64K+"};
  for (int p = 0; p < 5; p++) row(std::string("offset in article (coded bytes): ") + ofn[p], by_off[p]);
  row("half 1", by_half[0]); row("half 2", by_half[1]);
  for (auto& [k, a] : by_misc) row(k, a);
  printf("\nPROJECTION TABLE (page bytes only; gain per token = gain B / tokens)\n");
  printf("  %-12s %9s %10s %9s %9s %12s %12s %12s\n", "rep,df", "tokens", "bytes", "record B", "gain B", "gain B/tok", "half1 B/tok", "half2 B/tok");
  for (int c = 0; c < 25; c++) {
    std::string nm = c == 24 ? "separators" : "rep" + std::to_string(c / 6) + ",df" + std::to_string(c % 6);
    double g[2];
    for (int h = 0; h < 2; h++) g[h] = xht[h][c] ? (xh[h][c].base - xh[h][c].full) / 8 / xht[h][c] : 0.0;
    printf("  %-12s %9ld %10ld %9.0f %9.1f %12.5f %12.5f %12.5f\n", nm.c_str(), xtok[c], xc[c].bytes, xc[c].rec / 8,
           (xc[c].base - xc[c].full) / 8, xtok[c] ? (xc[c].base - xc[c].full) / 8 / xtok[c] : 0.0, g[0], g[1]);
  }
  if (gdfpath) {
    printf("\nPROJECTION TABLE by GLOBAL df (pages of full enwik9 containing the token: 1,2-9,10-99,100-999,1K-9.9K,10K+)\n");
    printf("  %-12s %9s %10s %9s %9s %12s %12s %12s\n", "rep,gdf", "tokens", "bytes", "record B", "gain B", "gain B/tok",
           "half1 B/tok", "half2 B/tok");
    for (int c = 0; c < 25; c++) {
      std::string nm = c == 24 ? "separators" : "rep" + std::to_string(c / 6) + ",gdf" + std::to_string(c % 6);
      double g[2];
      for (int h = 0; h < 2; h++) g[h] = xght[h][c] ? (xgh[h][c].base - xgh[h][c].full) / 8 / xght[h][c] : 0.0;
      printf("  %-12s %9ld %10ld %9.0f %9.1f %12.5f %12.5f %12.5f\n", nm.c_str(), xgt[c], xg[c].bytes, xg[c].rec / 8,
             (xg[c].base - xg[c].full) / 8, xgt[c] ? (xg[c].base - xg[c].full) / 8 / xgt[c] : 0.0, g[0], g[1]);
    }
  }
  printf("  non-page bytes %ld, record %.0f B, gain %.1f B\n", nonpage.bytes, nonpage.rec / 8, (nonpage.base - nonpage.full) / 8);
  printf("\nORACLE per token (rep = earlier uses in this article: 0,1,2-4,5+; df = earlier articles bucket). Cache cost\n"
         "is only defined when the cache had the whole token; otherwise the record's cost is used for that token.\n");
  printf("  %-16s %9s %10s %10s %10s %11s %11s %9s\n", "subset", "tokens", "record B", "AU B", "AUR B", "oracleU B",
         "oracleR B", "bits/tok");
  for (auto& [k, o] : orc)
    printf("  %-16s %9ld %10.0f %10.0f %10.0f %11.0f %11.0f %9.2f\n", k.c_str(), o.toks, o.rec / 8, o.cu / 8, o.cr / 8,
           (o.rec - o.minu) / 8, (o.rec - o.minr) / 8, o.rec / o.toks);
  return 0;
}
