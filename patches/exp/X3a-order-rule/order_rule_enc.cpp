// order_rule_enc.cpp ([sub100mil X3a]): build-time tool for order_rule.h.
//   order_rule_enc enc   MAIN ORDERFILE OUT.res        residual for the fx2 order file (article numbers, one per
//                                                      line); decodes it again and checks the positions
//   order_rule_enc check MAIN RES MAIN_REORDERED       rebuilds the reordered page stream as article_reorder.h's
//                                                      reorder() would (rule + residual, then the unused pages in
//                                                      id order) and compares it byte for byte with MAIN_REORDERED
// MAIN is the compressor's .main (enwik9 without intro and coda; raw enwik9 also works for enc, with one more exception).
#include "order_rule.h"
#include <array>
#include <fstream>
#include <string>
#include <unordered_map>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace ordrule;
using namespace std;
static void WriteRes(const char* path, const vector<int64_t>& v) { FILE* f = fopen(path, "wb"); for (int64_t x : v) fprintf(f, "%lld\n", (long long)x); fclose(f); }
static const char* Map(const char* p, size_t& n) { int fd = open(p, O_RDONLY); struct stat st; if (fd < 0 || fstat(fd, &st)) { perror(p); exit(1); } n = st.st_size; return (const char*)mmap(0, n, PROT_READ, MAP_PRIVATE, fd, 0); }
int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: order_rule_enc enc MAIN ORDER OUT.res | check MAIN RES MAIN_REORDERED\n"); return 1; }
  string mode = argv[1];
  if (mode == "check") {
    vector<int> positions;
    if (!DecodeOrder(argv[2], argv[3], positions)) { fprintf(stderr, "decode failed\n"); return 2; }
    size_t n, rn; const char* m = Map(argv[2], n); const char* r = Map(argv[4], rn);
    vector<pair<size_t, size_t>> pg;   // byte range of every page: "  <page>" line .. "  </page>" line
    for (size_t i = 0; i < n;) { const char* e = (const char*)memchr(m + i, '\n', n - i); size_t j = e ? (size_t)(e - m) + 1 : n;
      if (j - i >= 8 && !memcmp(m + i, "  <page>", 8)) pg.push_back({i, 0});
      if (j - i >= 9 && !memcmp(m + i, "  </page>", 9)) pg.back().second = j;
      i = j; }
    vector<char> used(pg.size(), 0); for (int p : positions) used[p] = 1;
    for (size_t i = 0; i < pg.size(); ++i) if (!used[i]) positions.push_back((int)i);
    size_t o = 0;
    for (int p : positions) { size_t len = pg[p].second - pg[p].first;
      if (o + len > rn || memcmp(r + o, m + pg[p].first, len)) { fprintf(stderr, "MISMATCH at page %d, offset %zu\n", p, o); return 3; }
      o += len; }
    if (o != rn) { fprintf(stderr, "LENGTH MISMATCH %zu vs %zu\n", o, rn); return 3; }
    printf("check OK: %zu pages, %zu bytes identical to %s\n", positions.size(), o, argv[4]);
    return 0;
  }
  vector<Page> pages;
  ReadPages(argv[2], pages);
  // article number -> page index, exactly as article_reorder.h's reorder() builds it (including its overwrites)
  unordered_map<int, int> remap;
  { int c1 = -1, c2 = -1; for (size_t i = 0; i < pages.size(); ++i) { remap[c2] = c1; if (i == 0 || !pages[i - 1].redirect) ++c2; ++c1; } }
  fprintf(stderr, "pages %zu\n", pages.size());
  // ---- encoder
  vector<int32_t> target;
  { ifstream f(argv[3]); string l; while (getline(f, l)) target.push_back(remap[stoi(l)]); }
  vector<char> isunit(pages.size(), 0); for (int32_t x : target) isunit[x] = 1;
  vector<int64_t> out; vector<int64_t> exc; int64_t last = -1;
  for (size_t i = 0; i < pages.size(); ++i) if ((bool)isunit[i] != !pages[i].redirect) { exc.push_back((int64_t)i - last); last = (int64_t)i; }
  out.push_back((int64_t)exc.size()); for (int64_t e : exc) out.push_back(e);
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
    out.push_back(ncorr); for (int64_t c : corr) out.push_back(c);
  } else out.push_back(0);
  // clusters = maximal descending runs of the target inside a class; axis rank = order in the file
  vector<int32_t> axis(n); int32_t nrun = 0;
  for (size_t i = 0; i < target.size(); ++i) { if (i == 0 || target[i] > target[i - 1] || tk[dense[target[i]]] != tk[dense[target[i - 1]]]) ++nrun; axis[dense[target[i]]] = nrun - 1; }
  vector<int32_t> seq_of_axis(nrun, -1), clus_true(n); int32_t ns = 0;
  for (int32_t t = 0; t < n; ++t) { if (seq_of_axis[axis[t]] < 0) seq_of_axis[axis[t]] = ns++; clus_true[t] = seq_of_axis[axis[t]]; }
  out.push_back(nrun); for (int32_t a = 0; a < nrun; ++a) out.push_back(seq_of_axis[a]);
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
    out.push_back(sym);
    int s = sym >= P_ESC ? P_ESC : (int)sym;   // adaptive order-0 estimate (+ log2(n_seen) for an escape)
    h_adapt += -log2((double)freq[s] / ftot) + (sym >= P_ESC ? log2((double)max(1, n_seen)) : 0); freq[s] += 24; ftot += 24;
    clus[t] = c; V.commit(c, k);
  }
  WriteRes(argv[4], out);
  fprintf(stderr, "class corrections %ld; ", ncorr);
  { long h[6] = {0}; for (int32_t t = 0; t < n; ++t) ++h[tk[t]]; fprintf(stderr, "classes normal/census/image/disam/geodis/hndis %ld/%ld/%ld/%ld/%ld/%ld; ", h[0], h[1], h[2], h[3], h[4], h[5]); }
  fprintf(stderr, "units %d exceptions %zu clusters %d | new %ld esc %ld rank0 %ld rank1 %ld r2-3 %ld r4-15 %ld r16-63 %ld r64+ %ld\n",
          n, exc.size(), nrun, cnt_new, cnt_esc, cnt_r[0], cnt_r[1], cnt_r[2], cnt_r[3], cnt_r[4], cnt_r[5]);
  fprintf(stderr, "order-0 adaptive estimate of the symbols: %.0f B (%.2f bits/article); header values %zu\n", h_adapt / 8, h_adapt / n, hdr);
  vector<int32_t> pos; vector<int64_t> res = ReadResidual(argv[4]);
  if (!Decode(pages2, res, pos) || pos != target) { fprintf(stderr, "ROUND TRIP FAILED\n"); return 3; }
  fprintf(stderr, "round trip OK: %zu positions identical to article_reorder.h's\n", pos.size());
  return 0;
}
