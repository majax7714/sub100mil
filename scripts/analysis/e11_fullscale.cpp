// E11 full-scale token statistics: how the in-article repeat / document-frequency mix of tokens changes from the
// 10M slice to full enwik9 (and with the record's article reordering).
//
//   g++ -O2 -march=x86-64-v3 -o build/exp/E11/e11_fullscale scripts/analysis/e11_fullscale.cpp
//   build/exp/E11/e11_fullscale FULL.PRE [ORDER.txt] [slice_pages=1325] [tokhash=IN.bin gdfout=OUT.bin]
//
// FULL.PRE is `slicetool pre` of full enwik9 (identity order). Pages are `  <page>\n` .. `  </page>\n`. Tokens are
// TEXT-level maximal runs of [A-Za-z0-9] and bytes >= 0x80 (case-sensitive; the coded stream's tokens carry the
// case flag, so this matches them up to WRT details). For every token occurrence inside a page:
//   rep = earlier occurrences in the same page (0, 1, 2-4, 5+), df = number of earlier pages containing it
//   (0, 1, 2-4, 5-19, 20-99, 100+). Prints token counts per (rep, df) cell for: the first `slice_pages` pages
// (calibration against the 10M slice simulation), all pages in identity order, and, if ORDER.txt is given (one
// page index per line, the order the record codes pages in), that order.
// Global document frequency (gdf) = number of pages of full enwik9 that contain the token; it is a property of the
// token, independent of history and order. Also prints the full-scale (rep, gdf) table (gdf buckets 1, 2-9, 10-99,
// 100-999, 1K-9.9K, 10K+), and with tokhash= (from e11_tokhash.py) writes the gdf of every slice token as uint32.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

static inline bool wordc(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80; }
static inline uint64_t hbytes(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
  return h ^ (h >> 31);
}

struct Cell { double toks = 0, bytes = 0; };

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: e11_fullscale FULL.PRE [ORDER.txt] [slice_pages=N]\n"); return 1; }
  int slice_pages = 1325;
  const char* order_path = nullptr;
  const char* tokhash = nullptr;
  const char* gdfout = nullptr;
  for (int i = 2; i < argc; i++) {
    if (!strncmp(argv[i], "slice_pages=", 12)) slice_pages = atoi(argv[i] + 12);
    else if (!strncmp(argv[i], "tokhash=", 8)) tokhash = argv[i] + 8;
    else if (!strncmp(argv[i], "gdfout=", 7)) gdfout = argv[i] + 7;
    else order_path = argv[i];
  }
  std::ifstream f(argv[1], std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const uint8_t* D = (const uint8_t*)data.data();
  // pages
  std::vector<std::pair<size_t, size_t>> pages;
  size_t pos = data.find("  <page>\n");
  while (pos != std::string::npos) {
    size_t e = data.find("  </page>\n", pos);
    if (e == std::string::npos) break;
    e += 10;
    pages.push_back({pos, e});
    pos = data.find("  <page>\n", e);
  }
  // tokens of each page as hashes (+ lengths)
  std::vector<std::vector<uint64_t>> th(pages.size());
  size_t ntok = 0;
  for (size_t p = 0; p < pages.size(); p++) {
    size_t i = pages[p].first, e = pages[p].second;
    while (i < e) {
      if (!wordc(D[i])) { i++; continue; }
      size_t s = i;
      while (i < e && wordc(D[i])) i++;
      th[p].push_back(hbytes(D + s, i - s));
    }
    ntok += th[p].size();
  }
  fprintf(stderr, "%zu pages, %zu tokens\n", pages.size(), ntok);
  { std::string().swap(data); }

  auto run = [&](const std::vector<size_t>& order, const char* label) {
    std::unordered_map<uint64_t, uint32_t> df;
    df.reserve(16 << 20);
    Cell cell[4][6];
    double sep_bytes = 0;
    std::unordered_map<uint64_t, uint32_t> inart;
    for (size_t idx : order) {
      inart.clear();
      for (size_t k = 0; k < th[idx].size(); k++) {
        uint64_t h = th[idx][k];
        auto it = df.find(h);
        uint32_t d = it == df.end() ? 0 : it->second;
        uint32_t& r = inart[h];
        int rb = r == 0 ? 0 : r == 1 ? 1 : r < 5 ? 2 : 3;
        int db = d == 0 ? 0 : d == 1 ? 1 : d < 5 ? 2 : d < 20 ? 3 : d < 100 ? 4 : 5;
        cell[rb][db].toks += 1;
        r++;
      }
      for (auto& kv : inart) df[kv.first]++;
    }
    (void)sep_bytes;
    double T = 0;
    for (auto& row : cell) for (auto& c : row) T += c.toks;
    printf("\n### %s: %zu pages, %.0f tokens, %zu distinct\n", label, order.size(), T, df.size());
    printf("rep\\df");
    const char* dn[] = {"0", "1", "2-4", "5-19", "20-99", "100+"};
    for (auto d : dn) printf("\t%s", d);
    printf("\n");
    const char* rn[] = {"0", "1", "2-4", "5+"};
    for (int r = 0; r < 4; r++) {
      printf("%s", rn[r]);
      for (int d = 0; d < 6; d++) printf("\t%.0f", cell[r][d].toks);
      printf("\n");
    }
    fflush(stdout);
  };
  // global document frequency
  std::unordered_map<uint64_t, uint32_t> gdf;
  {
    std::unordered_map<uint64_t, char> seen;
    for (auto& v : th) {
      seen.clear();
      for (uint64_t h : v) if (!seen.count(h)) { seen[h] = 1; gdf[h]++; }
    }
  }
  {
    double cell[4][6] = {};
    std::unordered_map<uint64_t, uint32_t> inart;
    for (auto& v : th) {
      inart.clear();
      for (uint64_t h : v) {
        uint32_t& r = inart[h];
        uint32_t g = gdf[h];
        int rb = r == 0 ? 0 : r == 1 ? 1 : r < 5 ? 2 : 3;
        int gb = g < 2 ? 0 : g < 10 ? 1 : g < 100 ? 2 : g < 1000 ? 3 : g < 10000 ? 4 : 5;
        cell[rb][gb] += 1;
        r++;
      }
    }
    printf("\n### full enwik9, (in-article repeat, global df) token counts (order-independent)\nrep\\gdf\t1\t2-9\t10-99\t100-999\t1K-9.9K\t10K+\n");
    const char* rn[] = {"0", "1", "2-4", "5+"};
    for (int r = 0; r < 4; r++) {
      printf("%s", rn[r]);
      for (int g = 0; g < 6; g++) printf("\t%.0f", cell[r][g]);
      printf("\n");
    }
  }
  if (tokhash && gdfout) {
    std::ifstream g(tokhash, std::ios::binary);
    std::vector<uint64_t> hs;
    uint64_t h;
    while (g.read((char*)&h, 8)) hs.push_back(h);
    std::vector<uint32_t> out(hs.size());
    size_t miss = 0;
    for (size_t i = 0; i < hs.size(); i++) {
      auto it = gdf.find(hs[i]);
      out[i] = it == gdf.end() ? 0 : it->second;
      miss += it == gdf.end();
    }
    std::ofstream o(gdfout, std::ios::binary);
    o.write((const char*)out.data(), out.size() * 4);
    printf("\nwrote gdf of %zu slice tokens to %s (%zu not found in full enwik9)\n", hs.size(), gdfout, miss);
  }
  std::vector<size_t> o;
  for (size_t i = 0; i < (size_t)slice_pages && i < pages.size(); i++) o.push_back(i);
  run(o, "identity order, first slice_pages pages (calibration)");
  o.clear();
  for (size_t i = 0; i < pages.size(); i++) o.push_back(i);
  run(o, "identity order, full enwik9");
  if (order_path) {
    o.clear();
    std::ifstream g(order_path);
    size_t v;
    while (g >> v) o.push_back(v);
    run(o, "record's -e order, full enwik9");
  }
  return 0;
}
