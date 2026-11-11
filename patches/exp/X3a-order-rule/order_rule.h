// order_rule.h ([sub100mil X3a]): the article order as a content rule plus a residual.
//
// The fx2 order file is not an arbitrary permutation. It was made (fx2-cmix src/article_order/*.ipynb) by embedding
// title + 4,000 characters of every article, t-SNE to ONE axis, k-means on that axis into N/80 clusters, each
// cluster written in descending article number, and then a stable sort that moves census, image, disambiguation,
// geodis and hndis pages to the end. So the permutation is (a) each article's cluster, (b) the clusters' axis order
// and (c) the page class, and (c) is an exact function of the text while (a) is mostly predictable from it.
//
// The compressor rebuilds the order from .main with this rule and a residual (comp9's order block): articles are
// visited in page order; hashed features (title tokens, links and words of the lead, categories, templates,
// links) vote with an integer IDF for the clusters of the last ORD_WIN earlier articles sharing each feature;
// votes are smeared onto axis neighbours, normalised by cluster fill, and the last two clusters of the same page
// class get a bonus. Residual symbol per article (stored class by class): 0 = opens a cluster, 1 + r = the same-class cluster of rank r in
// the vote, ORD_ESC + c = cluster c. The decompressor needs none of this (it sorts pages by id).
// The scheme (rule + residual, vote for clusters with axis smoothing) is cmix-neif's (its order_codec.h, GPL);
// this is an independent implementation for the UNMODIFIED fx2 order, so the coded stream does not change.
// Integer arithmetic only. Encoder: order_rule_enc.cpp (same header, so the round trip holds by construction).
#ifndef ORDER_RULE_H
#define ORDER_RULE_H
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
namespace ordrule {
using std::vector; using std::min; using std::max; using std::swap;
// Runs once, in the compressor only: keep it small rather than fast (the exe is in comp9 and in archive9).
#define ORD_COLD __attribute__((cold, noinline))
enum { P_DFMAX = 20000, P_MINDF = 2, P_WIN = 32, P_AXISK = 8, P_AXISW = 2, P_NORMK = 16, P_LEAD = 600, P_MAXCW = 160,
       P_MAXL = 200, P_REC1 = 6, P_REC2 = 4, P_ESC = 4096, P_CH = 0x3f, P_CLS = 1 };

static uint64_t Fnv(uint8_t type, const char* s, size_t len) {
  uint64_t h = 1469598103934665603ULL ^ type; h *= 1099511628211ULL;
  for (size_t i = 0; i < len; ++i) { h ^= (uint8_t)s[i]; h *= 1099511628211ULL; }
  return h;
}
static inline bool isal(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline bool isan(char c) { return isal(c) || (c >= '0' && c <= '9'); }
static inline char lowc(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
// Integer IDF. ilog2q8(x) = 256 * log2(x) in fixed point: the integer part by bit scan, the fraction from a
// 65-entry table of log2(1 + i/64) (built with integer squarings), linearly interpolated.
ORD_COLD static int64_t ilog2q8(uint64_t x) {
  int k = 63; while (!((x >> k) & 1)) --k;
  uint64_t m = k >= 16 ? x >> (k - 16) : x << (16 - k);  // mantissa in [65536, 131072)
  static int tab[65]; static bool init = false;
  if (!init) {
    for (int i = 0; i <= 64; i++) {
      uint64_t v = (uint64_t)(64 + i) << 26;               // 1 + i/64 in Q32
      int r = 0;                                           // 12 fraction bits of log2(v), one per squaring
      for (int b = 0; b < 12; b++) { v = (v * v) >> 32; r <<= 1; if (v >= (2ULL << 32)) { v >>= 1; r |= 1; } }
      tab[i] = (r + 8) >> 4;
    }
    init = true;
  }
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
  ORD_COLD void flags(const char* s, size_t n) {
    static const char* CEN[9] = {"According to the [[United States Census Bureau]],", "According to the [[U.S. Census Bureau]],",
      "As of the [[2000]] census,", "According to the [[2000]] census, ", "According to the [[United States Census Bureau|U.S. Census Bureau]],",
      "According to the [[US Census Bureau]], ", "According to the U.S. Census,", "As of the 2000 ", "As of the census of 2000,"};
    if (!f_cen) for (auto c : CEN) if (memmem(s, n, c, strlen(c))) { f_cen = true; break; }
    if (!f_hnd && memmem(s, n, "{{hndis", 7)) f_hnd = true;
    if (!f_geo && memmem(s, n, "{{geodis}}", 10)) f_geo = true;
    if (!f_dis) for (size_t i = 0; i + 9 <= n; ++i)     // {{[Dd]isam.*}} within a line
      if (s[i] == '{' && s[i + 1] == '{' && (s[i + 2] == 'D' || s[i + 2] == 'd') && !memcmp(s + i + 3, "isam", 4) && memmem(s + i + 7, n - i - 7, "}}", 2)) { f_dis = true; break; }
  }
  ORD_COLD void title(const char* a, size_t n) {
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
  ORD_COLD void leadline(const char* s, size_t n) {
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
  ORD_COLD void featsline(const char* s, size_t n) {
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

static bool starts(const char* l, size_t n, const char* p) { size_t m = strlen(p); return n >= m && !memcmp(l, p, m); }

// Reads the page stream (.main; raw enwik9 works too). A page is a redirect by the prefix test of
// article_reorder.h's reorder(); like there, the flag is set by any line of the page.
ORD_COLD static void ReadPages(const char* path, vector<Page>& pages) {
  FILE* in = fopen(path, "rb"); if (!in) return;
  char* s = nullptr; size_t cap = 0; ssize_t got; Extractor ex; bool in_page = false, redirect = false;
  static const char* RED[5] = {"#REDIRECT", "#redirect", "#Redirect", "#REdirect", "{{softredirect"};
  static const char TEXT[] = "      <text xml:space=\"preserve\">";
  while ((got = getline(&s, &cap, in)) >= 0) {
    size_t n = (size_t)got; if (n && s[n - 1] == '\n') --n;
    bool open = starts(s, n, TEXT);
    if (open) for (auto p : RED) if (starts(s + 33, n - 33, p)) { redirect = true; break; }
    if (n == 8 && !memcmp(s, "  <page>", 8)) { redirect = false; pages.emplace_back(); ex.reset(); in_page = true; continue; }
    if (!in_page) continue;
    if (starts(s, n, "  </page>")) {
      Page& p = pages.back(); p.redirect = redirect; p.klass = (uint8_t)ex.klass();
      std::sort(ex.f.begin(), ex.f.end()); ex.f.erase(std::unique(ex.f.begin(), ex.f.end()), ex.f.end());
      p.feats.swap(ex.f); in_page = false; continue;
    }
    if (const char* t = (const char*)memmem(s, n, "<title>", 7)) {
      const char* e = (const char*)memmem(t + 7, n - (size_t)(t + 7 - s), "</title>", 8);
      if (e) { ex.title(t + 7, (size_t)(e - t - 7));
        if (e - t - 7 >= 6 && !memcmp(t + 7, "Image:", 6)) ex.f_img = true;
        if (memmem(t + 7, (size_t)(e - t - 7), "(disambiguation)", 16)) ex.f_dis = true; }
    }
    if (open) ex.in_text = true;
    if (ex.in_text) {
      const char* e = (const char*)memmem(s, n, "</text>", 7);
      size_t from = open ? 33 : 0, to = e ? (size_t)(e - s) : n;
      if (to > from) { ex.flags(s + from, to - from); ex.leadline(s + from, to - from); }
      if (e) ex.in_text = false;
    }
    ex.featsline(s, n);
  }
  free(s); fclose(in);
}

struct Vote {
  int nrun = 0; vector<int64_t> score, smear; vector<int32_t> stamp, sstamp, fill, touch, axis_of_seq;
  const vector<int32_t>* seq_of_axis = nullptr;   // decoder: known for every cluster; encoder: same data
  ORD_COLD void init(int nr, const vector<int32_t>& startsv) {
    nrun = nr; score.assign(nr + 2, 0); smear.assign(nr + 2, 0); stamp.assign(nr + 2, -1); sstamp.assign(nr + 2, -1);
    fill.assign(nr + 2, 0); axis_of_seq.assign(nr, -1); ccls.assign(nr + 2, 0);
    for (int a = 0; a < nr; ++a) axis_of_seq[startsv[a]] = a;
    seq_of_axis = &startsv;
  }
  ORD_COLD void run(int32_t t, const vector<uint64_t>& nf, const vector<vector<int32_t>>& post, const vector<int64_t>& idf,
           const vector<int32_t>& clus, int32_t n_seen, int k) {
    touch.clear();
    for (uint64_t ff : nf) {
      int32_t f = (int32_t)ff; int64_t w = idf[f]; if (w <= 0) continue;
      const vector<int32_t>& pl = post[f];
      size_t hi = std::lower_bound(pl.begin(), pl.end(), t) - pl.begin();
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
  ORD_COLD void build(vector<Page>& pages, const vector<char>& isunit) {
    for (size_t i = 0; i < pages.size(); ++i) if (isunit[i]) { uni.push_back((int32_t)i); kl.push_back(P_CLS ? pages[i].klass : 0); feats.push_back(std::move(pages[i].feats)); }
    int32_t n = (int32_t)uni.size();
    // feature id = rank of its hash among the hashes kept (document frequency in [P_MINDF, P_DFMAX])
    vector<uint64_t> all; for (auto& v : feats) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());
    vector<uint64_t> key;
    for (size_t i = 0, j; i < all.size(); i = j) { for (j = i; j < all.size() && all[j] == all[i]; ++j) {} if (j - i >= P_MINDF && j - i <= P_DFMAX) key.push_back(all[i]); }
    vector<uint64_t>().swap(all);
    post.resize(key.size());
    for (int32_t t = 0; t < n; ++t) {
      vector<uint64_t> kept;
      for (uint64_t f : feats[t]) { size_t id = std::lower_bound(key.begin(), key.end(), f) - key.begin();
        if (id < key.size() && key[id] == f) { post[id].push_back(t); kept.push_back((uint64_t)id); } }
      feats[t].swap(kept);
    }
    idf.resize(post.size());
    for (size_t k = 0; k < post.size(); ++k) idf[k] = Idf((uint64_t)n, post[k].size());
  }
};

// Decoder: residual + pages -> positions (page indices of the articles, in coding order).
ORD_COLD static bool Decode(vector<Page>& pages, const vector<int64_t>& res, vector<int32_t>& positions) {
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
  // the symbols are stored class by class (six sections, each in page order)
  size_t cur[7] = {p, 0, 0, 0, 0, 0, 0};
  for (int32_t t = 0; t < n; ++t) ++cur[M.kl[t] + 1];
  for (int k = 1; k < 7; ++k) cur[k] += cur[k - 1];
  if (cur[6] != res.size()) return false;
  vector<int32_t> clus(n, -1); int32_t n_seen = 0;
  for (int32_t t = 0; t < n; ++t) {
    int k = M.kl[t];
    V.run(t, M.feats[t], M.post, M.idf, clus, n_seen, k);
    int64_t sym = res[cur[k]++]; int32_t cl;
    if (sym == 0) { cl = n_seen++; if (cl >= nrun) return false; V.ccls[cl] = (uint8_t)k; }
    else if (sym < P_ESC) {
      size_t m = V.filter(k), r = (size_t)(sym - 1); if (r >= m) return false;
      std::nth_element(V.touch.begin(), V.touch.begin() + r, V.touch.begin() + m, [&](int32_t a, int32_t b) { return V.better(a, b); });
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

ORD_COLD static vector<int64_t> ReadResidual(const char* path) {
  vector<int64_t> v; FILE* f = fopen(path, "rb"); if (!f) return v;
  long long x; while (fscanf(f, "%lld", &x) == 1) v.push_back(x);
  fclose(f); return v;
}
// main_path: the page stream (.main); residual_path: the decoded order block. positions: page indices of the
// articles in coding order (what article_reorder.h's reorder() calls positions, before the unused pages are appended).
ORD_COLD static bool DecodeOrder(const char* main_path, const char* residual_path, vector<int>& positions) {
  vector<Page> pages;
  ReadPages(main_path, pages);
  vector<int32_t> pos;
  if (!Decode(pages, ReadResidual(residual_path), pos)) return false;
  positions.assign(pos.begin(), pos.end());
  return true;
}
}  // namespace ordrule
#endif
