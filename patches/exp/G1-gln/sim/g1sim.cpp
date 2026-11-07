// Exact sim of the real G1 stage (g1_gln.h) over a G1 dump. usage: g1sim dump [n k lr lrmin bscale seed tau] [report_bits]
#include "g1_gln.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
  g1::Config c; long rep = 0;
  if (argc > 2) c.n = atoi(argv[2]); if (argc > 3) c.k = atoi(argv[3]); if (argc > 4) c.lr = atof(argv[4]);
  if (argc > 5) c.lrmin = atof(argv[5]); if (argc > 6) c.bscale = atof(argv[6]); if (argc > 7) c.seed = atoi(argv[7]);
  if (argc > 8) c.tau = atof(argv[8]); if (argc > 9) rep = atol(argv[9]);
  FILE* f = fopen(argv[1], "rb"); char hdr[8]; uint32_t n;
  if (!f || fread(hdr, 1, 8, f) != 8 || fread(&n, 4, 1, f) != 1) return 1;
  const int R = (n + 4) * 4 + 8; std::vector<unsigned char> buf((size_t)R * 65536);
  g1::Gln g(c); double base = 0, gl = 0, rb = 0, rg = 0; long bits = 0; size_t got;
  float X[g1::KX], Z[g1::ZD];
  while ((got = fread(buf.data(), R, 65536, f)) > 0)
    for (size_t r = 0; r < got; ++r) {
      const float* fl = (const float*)&buf[(size_t)r * R]; const unsigned char* bb = &buf[(size_t)r * R + (n + 4) * 4];
      int bit = bb[0]; unsigned q = (unsigned)(fl[n + 3] * 65536.0f + 0.5f), q2 = q;
      if (!bb[1]) {
        g1::Features(fl, n - 3, fl[n], fl[n + 1], g1::Stretch(q / 65536.0f), bb[5], X, Z);
        float p = g1::Squash(g.Predict(X, Z));
        q2 = (unsigned)(1.0f + 65534.0f * p); if (q2 < 1) q2 = 1; if (q2 > 65535) q2 = 65535;
        g.Update(bit);
      }
      double cb = -std::log2((bit ? q : 65536 - q) / 65536.0), cg = -std::log2((bit ? q2 : 65536 - q2) / 65536.0);
      base += cb; gl += cg; rb += cb; rg += cg; ++bits;
      if (rep && bits % rep == 0) { printf("  @%ld: %+.1f B\n", bits, (rg - rb) / 8); rb = rg = 0; }
    }
  printf("n=%d k=%d lr=%g lrmin=%g bscale=%g seed=%d tau=%g: bits %ld base %.1f B g1 %.1f B delta %+.1f B\n",
         c.n, c.k, c.lr, c.lrmin, c.bscale, c.seed, c.tau, bits, base / 8, gl / 8, (gl - base) / 8);
}
