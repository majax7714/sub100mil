// p1cm.h: the context-mixing model shared by the packer (pack.c) and the unpacking stub (stub.c).
// Integer-only, so the packer on the dev box and the stub on a test machine compute identical
// probabilities. Bitwise CM over an x86 executable (after the E8/E9 transform in xform()):
// NCTX hashed contexts with count-adaptive probabilities, a match model, one mixer selected by the
// bit position plus a mixer2 selected by the order-1 byte, averaged, then an order-1 APM.
#ifndef P1CM_H
#define P1CM_H
typedef unsigned int U32;
typedef unsigned short U16;
typedef unsigned char U8;

#ifndef LR1
#define LR1 2
#endif
#ifndef LR2
#define LR2 2
#endif
#ifndef LIM
#define LIM 8
#endif
#ifndef TB
#define TB 23
#endif
// TB: log2 slots per context table
#ifndef NCTX
#define NCTX 15               // hashed contexts
#endif
#ifndef APMW
#define APMW 3
#endif
#ifndef APMR
#define APMR 18
#endif
#ifndef WI
#define WI ((1 << 16) / 4)
#endif
#define NIN (NCTX + 2)        // + match + bias
#define MB 20                 // log2 match hash table

static U32 T[NCTX][1 << TB];  // p (22 bits) << 10 | count (10 bits)
static int W1[256 * 8][NIN];  // mixer 1 weights, by bit position x (c0 high bits)
static int W2[256 * 8][NIN];  // mixer 2 weights, by previous byte x bit position
static U16 APM[256 * 256][33];
static int MT[1 << MB];       // match table: last position of a hash of 6 bytes
static U32 MSM[64 * 2];       // match statemap: (len bucket, expected bit)
static short STR[4096];
static U8 *buf;               // history (the transformed bytes seen so far)
static U32 pos;               // bytes seen
static U32 c0 = 1, c4, c8, bitn, h[NCTX], idx[NCTX];
static int st[NIN], pr, pr1, pr2, apmi, apmw;
static U32 mptr, mlen, mbit, msmi;

static int squash(int d) {
  static const short t[33] = {1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546, 2047,
                              2549, 2994, 3348, 3607, 3785, 3901, 3975, 4022, 4050, 4068, 4079, 4085, 4089,
                              4092, 4093, 4094};
  if (d > 2047) d = 2047;
  if (d < -2047) d = -2047;
  int w = d & 127;
  d = (d >> 7) + 16;
  return (t[d] * (128 - w) + t[d + 1] * w + 64) >> 7;
}

static void cm_init(U8 *history) {
  buf = history;
  int pi = 0;
  for (int x = -2047; x <= 2047; x++) {
    int v = squash(x);
    for (int i = pi; i <= v; i++) STR[i] = (short)x;
    pi = v + 1;
  }
  for (int i = pi; i < 4096; i++) STR[i] = 2047;
  for (int i = 0; i < NCTX; i++)
    for (U32 j = 0; j < (1u << TB); j++) T[i][j] = 1u << 31;
  for (int i = 0; i < 256 * 8; i++)
    for (int j = 0; j < NIN; j++) W1[i][j] = W2[i][j] = WI;
  for (int i = 0; i < 256 * 256; i++)
    for (int j = 0; j < 33; j++) APM[i][j] = (U16)(squash((j - 16) * 128) * 16);
  for (int i = 0; i < 128; i++) MSM[i] = 1u << 31;
}

static inline U32 hsh(U32 a, U32 b) { return (a * 0x9E3779B1u) ^ ((b + 0x7F4A7C15u) * 0x85EBCA77u) ^ (a >> 15); }

// contexts for the next byte, computed at a byte boundary
static void byte_ctx(void) {
  U32 c1 = c4 & 0xff, c2 = c4 & 0xffff, c3 = c4 & 0xffffff;
  h[0] = 0;
  h[1] = hsh(1, c1);
  h[2] = hsh(2, c2);
  h[3] = hsh(3, c3);
  h[4] = hsh(4, c4);
  h[5] = hsh(5, hsh(c4, c8 & 0xffff));
  h[6] = hsh(6, hsh(c4, c8));
  h[7] = hsh(7, c4 & 0xff00ff00);        // sparse: bytes 2 and 4
  h[8] = hsh(8, c4 & 0xffff0000);        // sparse: bytes 3, 4
  h[9] = hsh(9, (c4 & 0xff00) | (pos & 3)); // byte 2 + alignment
  h[10] = hsh(10, c4 & 0x00ff00ff);
  h[11] = hsh(11, c2 | ((pos & 15) << 16));
#if NCTX > 12
  h[12] = hsh(12, (c8 & 0xff) | (c1 << 8));
#endif
#ifndef H13
#define H13 ((c4 >> 8) & 0xffff)
#endif
#if NCTX > 13
  h[13] = hsh(13, H13);
#endif
#ifndef H14
#define H14 (c8 & 0xff00ffff)
#endif
#if NCTX > 14
  h[14] = hsh(14, H14);
#endif
#ifdef SKIP
  h[SKIP] = SKIP;
#endif
#ifdef ALT
  h[ALT] = hsh(ALT, ALTX);
#endif
  // match model: find the last occurrence of the previous 6 bytes
  if (mlen && buf[mptr] == (U8)c1 && mlen < 65535) { mlen++; mptr++; }
  else mlen = 0;
  if (pos >= 6) {
    U32 mh = hsh(c4, c8 & 0xffff) >> (32 - MB);
    if (!mlen) {
      int p = MT[mh];
      if (p > 0) {
        U32 n = 0;
        while (n < 255 && n < (U32)p && buf[p - 1 - n] == buf[pos - 1 - n]) n++;
        if (n >= 6) { mlen = n; mptr = p; }
      }
    }
    MT[mh] = pos;
  }
}

static int cm_predict(void) {
  for (int i = 0; i < NCTX; i++) {
    idx[i] = (h[i] + c0 * 0x2F0B4A93u) >> (32 - TB);
    st[i] = STR[T[i][idx[i]] >> 20];
  }
  if (mlen) {
    mbit = (buf[mptr] >> (7 - bitn)) & 1;
    U32 lb = mlen > 31 ? 31 : mlen;
    msmi = lb * 2 + mbit;
    st[NCTX] = STR[MSM[msmi] >> 20];
  } else {
    msmi = 0;
    st[NCTX] = 0;
  }
  st[NCTX + 1] = 256;
  int *w1 = W1[c0 + 256 * (mlen ? (mlen < 16 ? 1 : 2) : 0)], *w2 = W2[(c4 & 0xff) * 8 + bitn];
  long long d1 = 0, d2 = 0;
  for (int i = 0; i < NIN; i++) { d1 += (long long)st[i] * w1[i]; d2 += (long long)st[i] * w2[i]; }
  pr1 = squash((int)(d1 >> 16));
  pr2 = squash((int)(d2 >> 16));
  int s = (STR[pr1] + STR[pr2]) >> 1;
  // APM on (order-1 byte, c0), interpolated over 33 buckets of the stretched mix
  int a = (s + 2048) * 32;
  apmw = a & 4095;
  apmi = ((c4 & 0xff) * 256 + c0) * 33 + (a >> 12);
  U16 *ap = &APM[0][0] + apmi;
  int pa = (ap[0] * (4096 - apmw) + ap[1] * apmw) >> 16;
  pr = (squash(s) * (4 - APMW) + APMW * pa) >> 2;
  if (pr < 1) pr = 1;
  if (pr > 4094) pr = 4094;
  return pr;
}

static inline void sm_upd(U32 *p, int y, U32 lim) {
  U32 v = *p, n = v & 1023, q = v >> 10;
  U32 tgt = y ? (1u << 22) - 1 : 0;
  if (n < lim) v++;
  *p = (v & 1023) | ((U32)((int)q + ((int)tgt - (int)q) / (int)(n + 2)) << 10);
}

static void cm_update(int y) {
  for (int i = 0; i < NCTX; i++) sm_upd(&T[i][idx[i]], y, LIM);
  if (mlen) sm_upd(&MSM[msmi], y, 1020);
  int *w1 = W1[c0 + 256 * (mlen ? (mlen < 16 ? 1 : 2) : 0)], *w2 = W2[(c4 & 0xff) * 8 + bitn];
  int e1 = ((y << 12) - pr1) * LR1, e2 = ((y << 12) - pr2) * LR2;
  for (int i = 0; i < NIN; i++) { w1[i] += (st[i] * e1) >> 10; w2[i] += (st[i] * e2) >> 10; }
  U16 *ap = &APM[0][0] + apmi;
  int g = (y << 16) + (y << 5) - y - y;
  ap[0] += (g - ap[0]) * (4096 - apmw) >> APMR;
  ap[1] += (g - ap[1]) * apmw >> APMR;
  if (mlen && (int)mbit != y) mlen = 0;
  c0 = c0 * 2 + y;
  bitn++;
  if (bitn == 8) {
    U8 c = (U8)c0;
    buf[pos++] = c;
    c8 = (c8 << 8) | (c4 >> 24);
    c4 = (c4 << 8) | c;
    c0 = 1;
    bitn = 0;
    byte_ctx();
  }
}

// x86 E8/E9 (call/jmp rel32) transform: rel -> abs (enc=1) or abs -> rel (enc=0). Same scan both ways.
static void xform(U8 *b, U32 n, int enc) {
  for (U32 i = 0; i + 5 <= n; i++) {
    if (b[i] == 0xE8 || b[i] == 0xE9) {
      U32 v = b[i + 1] | b[i + 2] << 8 | b[i + 3] << 16 | (U32)b[i + 4] << 24;
      v = enc ? v + i + 5 : v - i - 5;
      b[i + 1] = (U8)v; b[i + 2] = (U8)(v >> 8); b[i + 3] = (U8)(v >> 16); b[i + 4] = (U8)(v >> 24);
      i += 4;
    }
  }
}
#endif
