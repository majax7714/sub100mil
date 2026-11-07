// pack.c: p1cm packer. pack <in-exe> <out-payload>: E8/E9 transform + CM-compress <in-exe>; the payload is
// [u32 original size][arithmetic-coded bits]. Also decodes the payload again and checks it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p1cm.h"

static U32 x1, x2, xd;
static U8 *ob; static U32 on;
static const U8 *ib; static U32 in_, ip;

static void enc_bit(int y) {
  U32 p = (U32)cm_predict();
  U32 xm = x1 + (U32)(((unsigned long long)(x2 - x1) * p) >> 12);
  if (y) x2 = xm; else x1 = xm + 1;
  cm_update(y);
  while (((x1 ^ x2) & 0xff000000u) == 0) { ob[on++] = (U8)(x2 >> 24); x1 <<= 8; x2 = (x2 << 8) | 255; }
}
static int dec_bit(void) {
  U32 p = (U32)cm_predict();
  U32 xm = x1 + (U32)(((unsigned long long)(x2 - x1) * p) >> 12);
  int y = xd <= xm;
  if (y) x2 = xm; else x1 = xm + 1;
  cm_update(y);
  while (((x1 ^ x2) & 0xff000000u) == 0) { x1 <<= 8; x2 = (x2 << 8) | 255; xd = (xd << 8) | (ip < in_ ? ib[ip++] : 0); }
  return y;
}
static void reset(U8 *hist) { memset(T, 0, sizeof T); memset(MT, 0, sizeof MT); c0 = 1; c4 = c8 = bitn = pos = 0; mlen = 0;
  memset(h, 0, sizeof h); cm_init(hist); byte_ctx(); }

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: pack in out\n"); return 2; }
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); U32 n = (U32)ftell(f); fseek(f, 0, SEEK_SET);
  U8 *d = malloc(n), *orig = malloc(n), *hist = malloc(n + 8);
  if (fread(d, 1, n, f) != n) return 1; fclose(f); memcpy(orig, d, n);
  xform(d, n, 1);
  ob = malloc(n + 1024); on = 4; ob[0] = (U8)n; ob[1] = (U8)(n >> 8); ob[2] = (U8)(n >> 16); ob[3] = (U8)(n >> 24);
  reset(hist); x1 = 0; x2 = 0xffffffffu;
  for (U32 i = 0; i < n; i++) for (int j = 7; j >= 0; j--) enc_bit((d[i] >> j) & 1);
  ob[on++] = (U8)(x1 >> 24); ob[on++] = 255; ob[on++] = 255; ob[on++] = 255;
  // verify
  ib = ob + 4; in_ = on - 4; ip = 0; reset(hist); x1 = 0; x2 = 0xffffffffu; xd = 0;
  for (int k = 0; k < 4; k++) xd = (xd << 8) | ib[ip++];
  U8 *r = malloc(n);
  for (U32 i = 0; i < n; i++) { int c = 0; for (int j = 0; j < 8; j++) c = c * 2 + dec_bit(); r[i] = (U8)c; }
  xform(r, n, 0);
  if (memcmp(r, orig, n)) { fprintf(stderr, "VERIFY FAILED\n"); return 1; }
  f = fopen(argv[2], "wb"); fwrite(ob, 1, on, f); fclose(f);
  fprintf(stderr, "%u -> %u (verified)\n", n, on);
  return 0;
}
