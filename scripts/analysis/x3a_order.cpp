// X3a: article order as "rule + residual" for the fx2 order file, exact (the order itself is unchanged).
//
// Provenance of the order file (fx2-cmix README "Article order"; cmix-neif README 4.5): title + first 4,000
// characters of every article were embedded, arranged on ONE axis, cut into ~2,200 buckets of <= ~160 articles,
// and each bucket is written in DESCENDING article number. So the 172,277-entry permutation is really
//   (a) a partition of the articles into 3,800 maximal descending runs ("clusters"), and
//   (b) the order of those runs on the axis,
// and (a) is largely a function of the article text, which the compressor has. The idea of shipping the order as
// a content rule plus a residual is cmix-neif's (order_codec.h, V3 vote); this is an independent implementation
// of the same scheme, applied to the UNMODIFIED fx2 order so the coded stream (and the archive) does not change.
//
// Rule: articles are visited in ascending page index t. Hashed features (title tokens, links / words of the
// lead, categories, templates, links) with document frequency <= DFMAX vote with an integer IDF for the clusters
// of the earlier articles that share them; scores are smeared onto axis neighbours, normalised by cluster fill,
// and the clusters of the two previous articles get a bonus. Residual symbol per article:
//   0 = opens a new cluster, 1 + r = the cluster of rank r in the vote, ESC + c = cluster c (not voted for).
// Plus: the axis order of the clusters (one number per cluster) and the pages where "is an article" differs
// from the redirect prefix test.
//
//   x3a_order enc ENWIK9 ORDERFILE OUT.res [key=value ...]   encode, then decode and verify; prints statistics
//   x3a_order dec ENWIK9 RES OUT.positions                   decode only (what the compressor would run)
// Residual formats: fmt=text ("%d\n" lines, like the current order file) or fmt=bin (LEB128 varints).
// All integer arithmetic; no floating point in the rule (the IDF uses an integer log2 table in Q8).
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
using namespace std;

static int P_DFMAX = 175, P_AXISK = 8, P_AXISW = 2, P_NORMK = 16, P_LEAD = 600, P_MAXCW = 160, P_MAXL = 200;
static int P_REC1 = 3, P_REC2 = 2, P_ESC = 4096, P_BIN = 0, P_CH = 0x3f, P_MINDF = 2;
static int P_WIN = 0;
static int P_CLS = 1;  // 1 = page classes of the manual sort step restrict the candidate clusters  // 0 = all earlier articles vote; else only the last P_WIN postings per feature

static uint64_t Fnv(uint8_t type, const char* s, size_t len) {
  uint64_t h = 1469598103934665603ULL ^ type; h *= 1099511628211ULL;
  for (size_t i = 0; i < len; ++i) { h ^= (uint8_t)s[i]; h *= 1099511628211ULL; }
  return h;
}
static inline bool isal(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline bool isan(char c) { return isal(c) || (c >= '0' && c <= '9'); }
static inline char lowc(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
// integer IDF: round(256 * log2(n / df)) from a Q8 log2 by bit scan + 6-bit table interpolation
static int64_t ilog2q8(uint64_t x) {
  int k = 63; while (!((x >> k) & 1)) --k;
  uint64_t m = k >= 16 ? x >> (k - 16) : x << (16 - k);  // [65536, 131072)
  // log2(m/65536) in Q8 via 4 squarings-free linear pieces: use integer sqrt-free approximation table of 65 entries
  static int tab[65]; static bool init = false;
  if (!init) { for (int i = 0; i <= 64; i++) { // exact integer table: floor(256*log2(1+i/64)+0.5), computed with integers
      // use 40 iterations of squaring in fixed point
      uint64_t v = (uint64_t)(64 + i) << 26; // Q32 of (1+i/64)
      int r = 0; for (int b = 0; b < 12; b++) { v = (v * v) >> 32; r <<= 1; if (v >= (2ULL << 32)) { v >>= 1; r |= 1; } }
      tab[i] = (r + 8) >> 4; } init = true; }
  int idx = (int)((m - 65536) >> 10), fr = (int)((m - 65536) & 1023);
  return (int64_t)k * 256 + tab[idx] + (((int64_t)(tab[idx + 1] - tab[idx]) * fr) >> 10);
}
static int64_t Idf(uint64_t n, uint64_t df) { return ilog2q8(n) - ilog2q8(df); }

struct Page { bool redirect = false; uint8_t klass = 0; vector<uint64_t> feats; };

struct Extractor {
  vector<uint64_t> f; int ncw = 0, nl = 0; size_t lead = 0; bool in_text = false;
  bool f_cen = false, f_dis = false, f_geo = false, f_hnd = false, f_img = false;
  void reset() { f.clear(); ncw = nl = 0; lead = 0; in_text = false; f_cen = f_dis = f_geo = f_hnd = f_img = false; }
  // Page classes of the fx2 manual sort step (src/article_order/manual-sort.ipynb): the order file is the 1-D
  // cluster order, stably re-sorted so that census, image, disambiguation, geodis and hndis pages come last.
  int klass() const { return f_hnd ? 5 : f_geo ? 4 : f_dis ? 3 : f_img ? 2 : f_cen ? 1 : 0; }
  void flags(const char* s, size_t n) {
    static const char* CEN[9] = {"According to the [[United States Census Bureau]],", "According to the [[U.S. Census Bureau]],",
      "As of the [[2000]] census,", "According to the [[2000]] census, ", "According to the [[United States Census Bureau|U.S. Census Bureau]],",
      "According to the [[US Census Bureau]], ", "According to the U.S. Census,", "As of the 2000 ", "As of the census of 2000,"};
    if (!f_cen) for (auto c : CEN) if (memmem(s, n, c, strlen(c))) { f_cen = true; break; }
    if (!f_hnd && memmem(s, n, "{{hndis", 7)) f_hnd = true;
    if (!f_geo && memmem(s, n, "{{geodis}}", 10)) f_geo = true;
    if (!f_dis) for (size_t i = 0; i + 9 <= n; ++i)     // {{[Dd]isam.*}} within a line
      if (s[i] == '{' && s[i + 1] == '{' && (s[i + 2] == 'D' || s[i + 2] == 'd') && !memcmp(s + i + 3, "isam", 4) && memmem(s + i + 7, n - i - 7, "}}", 2)) { f_dis = true; break; }
  }
  void title(const char* a, size_t n) {
    char buf[64]; size_t i = 0;
    while (i < n) {
      while (i < n && !isan(a[i])) ++i;
      size_t s = i; while (i < n && isan(a[i])) ++i;
      size_t len = i - s;
      if (len >= 2) { if (len > 63) len = 63; for (size_t t = 0; t < len; ++t) buf[t] = lowc(a[s + t]); if (P_CH & 1) f.push_back(Fnv('W', buf, len)); }
    }
  }
  static bool link(const char* s, size_t m, size_t& i, const char*& x, size_t& len) {
    const char* hit = (const char*)memmem(s + i, m - i, "[[", 2);
    if (!hit) { i = m; return false; }
    size_t a = (size_t)(hit - s) + 2, b = a;
    while (b < m && b - a < 81 && s[b] != ']' && s[b] != '|' && s[b] != ':' && s[b] != '#') ++b;
    size_t rl = b - a;
    if (rl >= 1 && rl <= 80 && b < m && (s[b] == ']' || s[b] == '|')) {
      const char* p = s + a; const char* q = s + b;
      while (p < q && (uint8_t)*p <= ' ') ++p; while (q > p && (uint8_t)q[-1] <= ' ') --q;
      x = p; len = (size_t)(q - p); if (len > 60) len = 60; i = b + 1; return len > 0;
    }
    i = a - 1; return false;
  }
  void leadline(const char* s, size_t n) {
    if (lead >= (size_t)P_LEAD) return;
    size_t m = min(n, (size_t)P_LEAD - lead);
    if (P_CH & 2) for (size_t i = 0; i + 2 <= m;) { const char* x; size_t len; if (link(s, m, i, x, len)) f.push_back(Fnv('l', x, len)); }
    char buf[64]; size_t i = 0;
    while (i < m && ncw < P_MAXCW) {
      while (i < m && !isal(s[i])) ++i;
      size_t st = i; while (i < m && isal(s[i])) ++i;
      size_t len = i - st;
      if (len >= 5) { if (len > 63) len = 63; for (size_t t = 0; t < len; ++t) buf[t] = lowc(s[st + t]); if (P_CH & 4) f.push_back(Fnv('w', buf, len)); ++ncw; }
    }
    lead += m;
  }
  void featsline(const char* s, size_t n) {
    if (P_CH & 8) for (size_t i = 0; i + 11 <= n;) {
      const char* hit = (const char*)memmem(s + i, n - i, "[[Category:", 11);
      if (!hit) break;
      size_t a = (size_t)(hit - s) + 11, b = a;
      while (b < n && b - a < 120 && s[b] != ']' && s[b] != '|' && s[b] != '#') ++b;
      if (b > a) f.push_back(Fnv('C', s + a, b - a));
      i = b > a ? b : a;
    }
    if (P_CH & 16) for (size_t i = 0; i + 3 <= n;) {
      const char* hit = (const char*)memmem(s + i, n - i, "{{", 2);
      if (!hit) break;
      size_t a = (size_t)(hit - s) + 2;
      if (a >= n || !isal(s[a])) { i = (size_t)(hit - s) + 1; continue; }
      size_t b = a + 1; while (b < n && b - a < 41 && s[b] != '}' && s[b] != '|') ++b;
      char buf[48]; size_t len = b - a; if (len > 40) len = 40;
      while (len && (uint8_t)s[a + len - 1] <= ' ') --len;
      for (size_t t = 0; t < len; ++t) buf[t] = lowc(s[a + t]);
      if (len) f.push_back(Fnv('T', buf, len));
      i = b;
    }
    if (P_CH & 32) for (size_t i = 0; i + 2 <= n && nl < P_MAXL;) { const char* x; size_t len; if (link(s, n, i, x, len)) { f.push_back(Fnv('L', x, len)); ++nl; } }
  }
};

static bool starts(const string& l, const char* p) { size_t n = strlen(p); return l.size() >= n && !memcmp(l.data(), p, n); }

// Reads enwik9 (the page lines are what article_reorder.h sees in .main). Fills pages and the article-number remap
// exactly as article_reorder.h's reorder() builds it.
static void ReadPages(const char* path, vector<Page>& pages, unordered_map<int, int>& remap) {
  ifstream in(path); string l; Extractor ex; bool in_page = false, redirect = false; int c1 = -1, c2 = -1;
  static const char* RED[5] = {"      <text xml:space=\"preserve\">#REDIRECT", "      <text xml:space=\"preserve\">#redirect",
    "      <text xml:space=\"preserve\">#Redirect", "      <text xml:space=\"preserve\">#REdirect", "      <text xml:space=\"preserve\">{{softredirect"};
  while (getline(in, l)) {
    for (auto p : RED) if (starts(l, p)) { redirect = true; break; }
    if (l == "  <page>") {
      remap[c2] = c1; if (!redirect) ++c2; ++c1; redirect = false;
      pages.emplace_back(); ex.reset(); in_page = true; continue;
    }
    if (!in_page) continue;
    if (starts(l, "  </page>")) {
      Page& p = pages.back(); p.redirect = redirect; p.klass = (uint8_t)ex.klass();
      sort(ex.f.begin(), ex.f.end()); ex.f.erase(unique(ex.f.begin(), ex.f.end()), ex.f.end());
      p.feats.swap(ex.f); in_page = false; continue;   // NB: 'redirect' stays set until the next <page>, as upstream
    }
    const char* s = l.data(); size_t n = l.size();
    if (const char* t = (const char*)memmem(s, n, "<title>", 7)) {
      const char* e = (const char*)memmem(t + 7, n - (size_t)(t + 7 - s), "</title>", 8);
      if (e) { ex.title(t + 7, (size_t)(e - t - 7));
        if (e - t - 7 >= 6 && !memcmp(t + 7, "Image:", 6)) ex.f_img = true;
        if (memmem(t + 7, (size_t)(e - t - 7), "(disambiguation)", 16)) ex.f_dis = true; }
    }
    bool open = starts(l, "      <text xml:space=\"preserve\">");
    if (open) ex.in_text = true;
    if (ex.in_text) {
      const char* e = (const char*)memmem(s, n, "</text>", 7);
      size_t from = open ? 33 : 0, to = e ? (size_t)(e - s) : n;
      if (to > from) { ex.flags(s + from, to - from); ex.leadline(s + from, to - from); }
      if (e) ex.in_text = false;
    }
    ex.featsline(s, n);
  }
}

struct Vote {
  int nrun = 0; vector<int64_t> score, smear; vector<int32_t> stamp, sstamp, fill, touch, axis_of_seq;
  const vector<int32_t>* seq_of_axis = nullptr;   // decoder: known for every cluster; encoder: same data
  void init(int nr, const vector<int32_t>& startsv) {
    nrun = nr; score.assign(nr + 2, 0); smear.assign(nr + 2, 0); stamp.assign(nr + 2, -1); sstamp.assign(nr + 2, -1);
    fill.assign(nr + 2, 0); axis_of_seq.assign(nr, -1); ccls.assign(nr + 2, 0);
    for (int a = 0; a < nr; ++a) axis_of_seq[startsv[a]] = a;
    seq_of_axis = &startsv;
  }
  void run(int32_t t, const vector<uint64_t>& nf, const vector<vector<int32_t>>& post, const vector<int64_t>& idf,
           const vector<int32_t>& clus, int32_t n_seen, int k) {
    touch.clear();
    for (uint64_t ff : nf) {
      int32_t f = (int32_t)ff; int64_t w = idf[f]; if (w <= 0) continue;
      const vector<int32_t>& pl = post[f];
      size_t hi = lower_bound(pl.begin(), pl.end(), t) - pl.begin();
      size_t lo = (P_WIN && hi > (size_t)P_WIN) ? hi - P_WIN : 0;
      for (size_t k = lo; k < hi; ++k) {
        int32_t cs = clus[pl[k]];
        if (stamp[cs] != t) { stamp[cs] = t; score[cs] = 0; touch.push_back(cs); }
        score[cs] += w;
      }
    }
    if (!touch.empty() && P_AXISW) {
      const size_t nb0 = touch.size();
      for (size_t bi = 0; bi < nb0; ++bi) {
        int32_t cs = touch[bi], a = axis_of_seq[cs]; int64_t w = (score[cs] * P_AXISW) >> 4;
        for (int d = 1; d <= P_AXISK && w > 0; ++d, w >>= 1)
          for (int sg = -1; sg <= 1; sg += 2) {
            int32_t ax = a + sg * d; if (ax < 0 || ax >= nrun) continue;
            int32_t nb = (*seq_of_axis)[ax]; if (nb >= n_seen) continue;
            if (stamp[nb] != t) { stamp[nb] = t; score[nb] = 0; touch.push_back(nb); }
            if (sstamp[nb] != t) { sstamp[nb] = t; smear[nb] = 0; }
            smear[nb] += w;
          }
      }
      for (int32_t cs : touch) if (sstamp[cs] == t) { score[cs] += smear[cs]; sstamp[cs] = -1; }
    }
    for (int32_t cs : touch) score[cs] = score[cs] * 64 / (fill[cs] + P_NORMK);
    int64_t mx = 0; for (int32_t x : touch) mx = max(mx, score[x]);
    if (rec[k][0] >= 0) add(rec[k][0], mx * P_REC1 / 16, t);
    if (rec[k][1] >= 0) add(rec[k][1], mx * P_REC2 / 16, t);
  }
  int32_t rec[6][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};   // last two distinct clusters per class
  vector<uint8_t> ccls;                                                                  // class of each cluster
  void commit(int32_t c, int k) { ++fill[c]; if (rec[k][0] != c) { rec[k][1] = rec[k][0]; rec[k][0] = c; } }
  // candidates of class k, in touch[0..m)
  size_t filter(int k) { size_t m = 0; for (size_t i = 0; i < touch.size(); ++i) if (ccls[touch[i]] == k) swap(touch[m++], touch[i]); return m; }
  void add(int32_t c, int64_t b, int32_t t) {
    if (stamp[c] != t) { stamp[c] = t; score[c] = 0; touch.push_back(c); }
    score[c] += b > 1 ? b : 1;
  }
  bool better(int32_t a, int32_t b) const { return score[a] != score[b] ? score[a] > score[b] : a < b; }
};

struct Model {   // shared by encoder and decoder: features -> postings, idf
  vector<int32_t> uni; vector<uint8_t> kl; vector<vector<uint64_t>> feats; vector<vector<int32_t>> post; vector<int64_t> idf;
  void build(vector<Page>& pages, const vector<char>& isunit) {
    for (size_t i = 0; i < pages.size(); ++i) if (isunit[i]) { uni.push_back((int32_t)i); kl.push_back(P_CLS ? pages[i].klass : 0); feats.push_back(move(pages[i].feats)); }
    int32_t n = (int32_t)uni.size();
    unordered_map<uint64_t, int32_t> df; df.reserve((size_t)n * 16);
    for (auto& v : feats) for (uint64_t f : v) ++df[f];
    // feature ids in order of first appearance (deterministic, independent of the hash map's iteration order)
    unordered_map<uint64_t, int32_t> fid; fid.reserve(df.size());
    for (int32_t t = 0; t < n; ++t) {
      vector<uint64_t> kept;
      for (uint64_t f : feats[t]) { int d = df[f]; if (d > P_DFMAX || d < P_MINDF) continue;
        auto it = fid.find(f); int32_t id;
        if (it == fid.end()) { id = (int32_t)fid.size(); fid.emplace(f, id); post.emplace_back(); } else id = it->second;
        post[id].push_back(t); kept.push_back((uint64_t)id); }
      feats[t].swap(kept);
    }
    idf.resize(post.size());
    for (size_t k = 0; k < post.size(); ++k) idf[k] = Idf((uint64_t)n, post[k].size());
  }
};

static void put(vector<int64_t>& o, int64_t v) { o.push_back(v); }
static void writeRes(const char* path, const vector<int64_t>& v) {
  FILE* f = fopen(path, "wb");
  if (P_BIN) for (int64_t x : v) { uint64_t u = (uint64_t)x; do { uint8_t c = u & 0x7f; u >>= 7; if (u) c |= 0x80; fputc(c, f); } while (u); }
  else for (int64_t x : v) fprintf(f, "%lld\n", (long long)x);
  fclose(f);
}
static vector<int64_t> readRes(const char* path) {
  vector<int64_t> v; FILE* f = fopen(path, "rb");
  if (P_BIN) { int c; uint64_t x = 0; int s = 0; while ((c = fgetc(f)) != EOF) { x |= (uint64_t)(c & 0x7f) << s; if (c & 0x80) s += 7; else { v.push_back((int64_t)x); x = 0; s = 0; } } }
  else { long long x; while (fscanf(f, "%lld", &x) == 1) v.push_back(x); }
  fclose(f); return v;
}

// Decoder: residual + pages -> positions (page indices of the articles, in coding order).
static bool Decode(vector<Page>& pages, const vector<int64_t>& res, vector<int32_t>& positions) {
  size_t p = 0; auto get = [&]() { return p < res.size() ? res[p++] : -1; };
  vector<char> isunit(pages.size());
  for (size_t i = 0; i < pages.size(); ++i) isunit[i] = !pages[i].redirect;
  int64_t ne = get(), last = -1;
  for (int64_t i = 0; i < ne; ++i) { last += get(); if (last < 0 || (size_t)last >= pages.size()) return false; isunit[last] ^= 1; }
  Model M; M.build(pages, isunit);
  int32_t n = (int32_t)M.uni.size();
  { int64_t nc = get(), lastc = -1; for (int64_t i = 0; i < nc; ++i) { lastc += get(); int64_t k = get(); if (lastc < 0 || lastc >= n || k < 0 || k > 5) return false; M.kl[lastc] = (uint8_t)k; } }
  int32_t nrun = (int32_t)get();
  vector<int32_t> startsv(nrun); for (auto& s : startsv) s = (int32_t)get();
  Vote V; V.init(nrun, startsv);
  vector<int32_t> clus(n, -1); int32_t n_seen = 0;
  for (int32_t t = 0; t < n; ++t) {
    int k = M.kl[t];
    V.run(t, M.feats[t], M.post, M.idf, clus, n_seen, k);
    int64_t sym = get(); int32_t cl;
    if (sym == 0) { cl = n_seen++; if (cl >= nrun) return false; V.ccls[cl] = (uint8_t)k; }
    else if (sym < P_ESC) {
      size_t m = V.filter(k), r = (size_t)(sym - 1); if (r >= m) return false;
      nth_element(V.touch.begin(), V.touch.begin() + r, V.touch.begin() + m, [&](int32_t a, int32_t b) { return V.better(a, b); });
      cl = V.touch[r];
    } else cl = (int32_t)(sym - P_ESC);
    if (cl < 0 || cl >= n_seen) return false;
    clus[t] = cl; V.commit(cl, k);
  }
  vector<vector<int32_t>> mem(nrun);
  for (int32_t t = 0; t < n; ++t) mem[clus[t]].push_back(t);
  positions.clear();
  for (int32_t cs : startsv) for (size_t i = mem[cs].size(); i-- > 0;) positions.push_back(M.uni[mem[cs][i]]);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: x3a_order enc ENWIK9 ORDER OUT.res [k=v..] | dec ENWIK9 RES OUT.positions [k=v..]\n"); return 1; }
  for (int i = 5; i < argc; ++i) {
    char* e = strchr(argv[i], '='); if (!e) continue; *e = 0; int v = atoi(e + 1); string k = argv[i];
    if (k == "dfmax") P_DFMAX = v; else if (k == "axisk") P_AXISK = v; else if (k == "axisw") P_AXISW = v; else if (k == "normk") P_NORMK = v;
    else if (k == "lead") P_LEAD = v; else if (k == "maxcw") P_MAXCW = v; else if (k == "maxl") P_MAXL = v; else if (k == "rec1") P_REC1 = v;
    else if (k == "rec2") P_REC2 = v; else if (k == "esc") P_ESC = v; else if (k == "bin") P_BIN = v; else if (k == "ch") P_CH = v;
    else if (k == "mindf") P_MINDF = v; else if (k == "cls") P_CLS = v; else if (k == "win") P_WIN = v;
    else { fprintf(stderr, "unknown %s\n", argv[i]); return 1; }
  }
  string mode = argv[1];
  vector<Page> pages; unordered_map<int, int> remap;
  ReadPages(argv[2], pages, remap);
  fprintf(stderr, "pages %zu\n", pages.size());
  if (mode == "dec") {
    vector<int64_t> res = readRes(argv[3]); vector<int32_t> pos;
    if (!Decode(pages, res, pos)) { fprintf(stderr, "decode failed\n"); return 2; }
    FILE* f = fopen(argv[4], "w"); for (int32_t x : pos) fprintf(f, "%d\n", x); fclose(f); return 0;
  }
  // ---- encoder
  vector<int32_t> target;
  { ifstream f(argv[3]); string l; while (getline(f, l)) target.push_back(remap[stoi(l)]); }
  vector<char> isunit(pages.size(), 0); for (int32_t x : target) isunit[x] = 1;
  vector<int64_t> out; vector<int64_t> exc; int64_t last = -1;
  for (size_t i = 0; i < pages.size(); ++i) if ((bool)isunit[i] != !pages[i].redirect) { exc.push_back((int64_t)i - last); last = (int64_t)i; }
  put(out, (int64_t)exc.size()); for (int64_t e : exc) put(out, e);
  vector<Page> pages2 = pages;   // the decoder gets its own copy
  Model M; M.build(pages, isunit);
  int32_t n = (int32_t)M.uni.size();
  vector<int32_t> dense(pages.size(), -1); for (int32_t t = 0; t < n; ++t) dense[M.uni[t]] = t;
  // true classes: the order is [normal | census | image | disam | geodis | hndis]; put the five boundaries where the
  // class rule disagrees least (DP), and list the disagreeing articles as corrections.
  vector<uint8_t> tk(n, 0); long ncorr = 0;
  if (P_CLS) {
    size_t N = target.size(); vector<array<int32_t, 6>> cost(N + 1); vector<array<uint8_t, 6>> from(N + 1);
    for (int k = 0; k < 6; ++k) cost[0][k] = 0;
    for (size_t i = 0; i < N; ++i) { int r = M.kl[dense[target[i]]]; int32_t best = INT32_MAX; uint8_t bk = 0;
      for (int k = 0; k < 6; ++k) { if (cost[i][k] < best) { best = cost[i][k]; bk = (uint8_t)k; } cost[i + 1][k] = best + (r != k); from[i + 1][k] = bk; } }
    int k = 0; for (int j = 1; j < 6; ++j) if (cost[N][j] < cost[N][k]) k = j;
    for (size_t i = N; i-- > 0;) { tk[dense[target[i]]] = (uint8_t)k; k = from[i + 1][k]; }
    vector<int64_t> corr; int64_t lastc = -1;
    for (int32_t t = 0; t < n; ++t) if (tk[t] != M.kl[t]) { corr.push_back(t - lastc); corr.push_back(tk[t]); lastc = t; M.kl[t] = tk[t]; ++ncorr; }
    put(out, ncorr); for (int64_t c : corr) put(out, c);
  } else put(out, 0);
  // clusters = maximal descending runs of the target inside a class; axis rank = order in the file
  vector<int32_t> axis(n); int32_t nrun = 0;
  for (size_t i = 0; i < target.size(); ++i) { if (i == 0 || target[i] > target[i - 1] || tk[dense[target[i]]] != tk[dense[target[i - 1]]]) ++nrun; axis[dense[target[i]]] = nrun - 1; }
  vector<int32_t> seq_of_axis(nrun, -1), clus_true(n); int32_t ns = 0;
  for (int32_t t = 0; t < n; ++t) { if (seq_of_axis[axis[t]] < 0) seq_of_axis[axis[t]] = ns++; clus_true[t] = seq_of_axis[axis[t]]; }
  put(out, nrun); for (int32_t a = 0; a < nrun; ++a) put(out, seq_of_axis[a]);
  size_t hdr = out.size();
  Vote V; V.init(nrun, seq_of_axis);
  vector<int32_t> clus(n, -1); int32_t n_seen = 0;
  long cnt_new = 0, cnt_esc = 0, cnt_r[6] = {0}; double h_adapt = 0; vector<int> freq(P_ESC + 1, 1); long ftot = P_ESC + 1;
  for (int32_t t = 0; t < n; ++t) {
    int k = M.kl[t];
    V.run(t, M.feats[t], M.post, M.idf, clus, n_seen, k);
    int32_t c = clus_true[t]; int64_t sym;
    if (c == n_seen) { sym = 0; ++n_seen; ++cnt_new; V.ccls[c] = (uint8_t)k; }
    else if (V.stamp[c] == t) {
      int64_t r = 0; for (int32_t x : V.touch) if (x != c && V.ccls[x] == k && V.better(x, c)) ++r;
      if (r + 1 < P_ESC) { sym = 1 + r; cnt_r[r == 0 ? 0 : r == 1 ? 1 : r < 4 ? 2 : r < 16 ? 3 : r < 64 ? 4 : 5]++; } else { sym = P_ESC + c; ++cnt_esc; }
    } else { sym = P_ESC + c; ++cnt_esc; }
    put(out, sym);
    int s = sym >= P_ESC ? P_ESC : (int)sym;   // adaptive order-0 estimate (+ log2(n_seen) for an escape)
    h_adapt += -log2((double)freq[s] / ftot) + (sym >= P_ESC ? log2((double)max(1, n_seen)) : 0); freq[s] += 24; ftot += 24;
    clus[t] = c; V.commit(c, k);
  }
  writeRes(argv[4], out);
  fprintf(stderr, "class corrections %ld; ", ncorr);
  { long h[6] = {0}; for (int32_t t = 0; t < n; ++t) ++h[tk[t]]; fprintf(stderr, "classes normal/census/image/disam/geodis/hndis %ld/%ld/%ld/%ld/%ld/%ld; ", h[0], h[1], h[2], h[3], h[4], h[5]); }
  fprintf(stderr, "units %d exceptions %zu clusters %d | new %ld esc %ld rank0 %ld rank1 %ld r2-3 %ld r4-15 %ld r16-63 %ld r64+ %ld\n",
          n, exc.size(), nrun, cnt_new, cnt_esc, cnt_r[0], cnt_r[1], cnt_r[2], cnt_r[3], cnt_r[4], cnt_r[5]);
  fprintf(stderr, "order-0 adaptive estimate of the symbols: %.0f B (%.2f bits/article); header values %zu\n", h_adapt / 8, h_adapt / n, hdr);
  vector<int32_t> pos; vector<int64_t> res = readRes(argv[4]);
  if (!Decode(pages2, res, pos) || pos != target) { fprintf(stderr, "ROUND TRIP FAILED\n"); return 3; }
  fprintf(stderr, "round trip OK: %zu positions identical to article_reorder.h's\n", pos.size());
  return 0;
}
