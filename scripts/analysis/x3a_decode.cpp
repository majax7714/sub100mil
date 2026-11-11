// X3a: decode the C3 full-run cost dump (build/exp/FULL/cost_full.bin) back to the reordered
// phda9 text it codes, with the per-character cost C3 paid.
//   x3a_decode COST.bin english.dic OUT.txt OUT.cost
// OUT.txt : decoded text (inverse WRT, same algorithm as a1_analyze.py wrt_decode)
// OUT.cost: float32 per text byte; the cost of every coded byte is put on the first text byte it
//           produces (flags and word codes -> first letter of the word). Sum over a span = bits paid.
// Streams the dump (40 B records); RAM = dictionary only.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
using namespace std;
static unsigned char SWAP[256];
int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage\n"); return 1; }
  for (int c = 0; c < 256; c++) {
    int d = c;
    if ('{' <= d && d < 127) d += 'P' - '{';
    else if ('P' <= d && d < 'T') d -= 'P' - '{';
    else if ((':' <= d && d <= '?') || ('J' <= d && d <= 'O')) d ^= 0x70;
    if (d == 'X' || d == '`') d ^= 'X' ^ '`';
    SWAP[c] = d;
  }
  // dictionary
  unordered_map<uint32_t, string> rev;
  { FILE* f = fopen(argv[2], "rb"); if (!f) { perror("dict"); return 1; }
    string data; char buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n); fclose(f);
    size_t i = 0; long idx = 0; const long b1 = 80, b2 = 80 + 3840, b3 = 80 + 3840 + 40960;
    while (i < data.size()) {
      if (data[i] < 'a' || data[i] > 'z') { i++; continue; }
      size_t j = i; while (j < data.size() && data[j] >= 'a' && data[j] <= 'z') j++;
      uint32_t code; long nn = idx;
      if (nn < b1) code = 0x80 + nn;
      else if (nn < b2) code = (0xD0 + (nn - b1) / 80) + ((0x80 + (nn - b1) % 80) << 8);
      else if (nn < b3) { long m = nn - b2; code = (0xF0 + (m / 80) / 32) + ((0xD0 + (m / 80) % 32) << 8) + ((0x80 + m % 80) << 16); }
      else break;
      rev[code] = data.substr(i, j - i); idx++; i = j;
    } }
  FILE* f = fopen(argv[1], "rb"); if (!f) { perror("cost"); return 1; }
  FILE* ot = fopen(argv[3], "wb"); FILE* oc = fopen(argv[4], "wb");
  static char ib[1 << 22], b1[1 << 22], b2[1 << 22];
  setvbuf(f, ib, _IOFBF, sizeof ib); setvbuf(ot, b1, _IOFBF, sizeof b1); setvbuf(oc, b2, _IOFBF, sizeof b2);
  unsigned char hdr[16]; if (fread(hdr, 1, 16, f) != 16) return 1;
  unsigned char rec[40];
  auto next = [&](int& c, float& cost) -> bool {
    if (fread(rec, 1, 40, f) != 40) return false;
    c = SWAP[rec[0]]; memcpy(&cost, rec + 24, 4); return true; };
  int c; float cost; double pend = 0, total = 0; uint64_t ncoded = 0, ntext = 0;
  if (fread(rec, 1, 40, f) != 40) return 1;  // WRT flag byte
  fprintf(stderr, "first byte %d\n", rec[0]); memcpy(&cost, rec + 24, 4); pend = cost; total = cost; ncoded = 1;
  bool upper = false, capital = false;
  auto emit = [&](const char* s, size_t n) {
    fwrite(s, 1, n, ot);
    float z = (float)pend; fwrite(&z, 4, 1, oc); pend = 0;
    z = 0; for (size_t k = 1; k < n; k++) fwrite(&z, 4, 1, oc);
    ntext += n; };
  while (next(c, cost)) {
    ncoded++; pend += cost; total += cost;
    if (c == 0x0C) { upper = false; int c2; float k2; if (!next(c2, k2)) break; ncoded++; pend += k2; total += k2; char ch = c2; emit(&ch, 1); }
    else if (c == 0x07) upper = true;
    else if (c == 0x40) capital = true;
    else if (c == 0x06) upper = false;
    else if (c >= 0x80) {
      uint32_t code = c;
      if (c > 0xCF) { int c2; float k2; if (!next(c2, k2)) break; ncoded++; pend += k2; total += k2; code += (uint32_t)c2 << 8;
        if (c2 > 0xCF) { int c3; float k3; if (!next(c3, k3)) break; ncoded++; pend += k3; total += k3; code += (uint32_t)c3 << 16; } }
      auto it = rev.find(code); if (it == rev.end()) { fprintf(stderr, "bad code %x at %llu\n", code, (unsigned long long)ncoded); return 2; }
      string w = it->second;
      if (capital) { w[0] -= 32; capital = false; }
      if (upper) for (auto& ch : w) ch = ch - 'a' + 'A';
      emit(w.data(), w.size());
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) upper = false;
      if (capital || upper) c = c - 'a' + 'A';
      capital = false; char ch = c; emit(&ch, 1);
    }
  }
  fclose(ot); fclose(oc);
  fprintf(stderr, "coded %llu text %llu total bits %.0f = %.0f B\n", (unsigned long long)ncoded, (unsigned long long)ntext, total, total / 8);
  return 0;
}
