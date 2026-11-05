// a1_match: retrieval-headroom analysis for the A1 design (docs/design/A1-retrieval.md).
//
// Input is either a per-byte cost log written by the record with FX2_COSTLOG
// (patches/tools/per-byte-cost/, magic "FX2COST\0") or a raw coded stream
// (e.g. the output of `cmix -s english.dic slice.pre out` minus its 5-byte header).
//
// For every coded position i it computes, causally (only s[0..i-1] is "history"):
//   * SAM (suffix automaton, --sam): L = length of the longest suffix of s[0..i-1] that also
//     occurred earlier; M = length of the longest suffix that occurred earlier *followed by s[i]*
//     (-1 if s[i] is a novel byte); nconts = number of distinct bytes that ever followed the
//     L-context.  M >= k means "some earlier occurrence of the k-context predicted s[i]" (oracle).
//   * Realistic most-recent-occurrence predictors, hash + byte-verified:
//       k8, k16, k32, k64 : most recent earlier occurrence of the exact k-byte context
//       lp                : longest-match model (follow the current match while it predicts
//                           correctly; on a miss re-look-up at k=64,32,16,8 and keep the longest
//                           verified candidate). Its verified length is reported.
//       skip16, skip32    : "template" match: context = the last k *normalized tokens*, where a
//                           digit run collapses to one token and a capitalized word (WRT 0x40
//                           flag + word bytes) or an all-caps word (0x07 flag) collapses to one
//                           token; the key also includes the offset inside the current run.
//                           So "born 1912 in [Paris]" matches "born 1887 in [Lyon]".
//   Each predictor yields a predicted byte (valid/correct flags) and a source distance.
//
// Outputs:
//   <out>.pos     per-position records (struct PosRec below), for joining with costs in Python
//   <out>.tsv     aggregate table: per window x predictor x distance bucket, valid/correct counts
//   with a cost log, also <out>.mix: per-byte float32 pairs (cost_base, cost_with_retrieval) of
//   a simulated final-stage mixer (see MixSim) and a summary on stdout.
//
// Build: g++ -O2 -march=x86-64-v3 -o a1_match a1_match.cpp
// Usage: a1_match [--sam] [--no-pos] [--window N] <costlog|stream> <outprefix>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

static void die(const char* m) { fprintf(stderr, "a1_match: %s\n", m); exit(1); }

// ---------------------------------------------------------------- suffix automaton
struct SAM {
  std::vector<int> len, link, head;          // per state
  std::vector<int> e_next, e_to; std::vector<u8> e_c;  // edge lists
  int last = 0;
  void reserve(size_t n) {
    len.reserve(2 * n + 2); link.reserve(2 * n + 2); head.reserve(2 * n + 2);
    e_next.reserve(3 * n + 300); e_to.reserve(3 * n + 300); e_c.reserve(3 * n + 300);
    len.push_back(0); link.push_back(-1); head.push_back(-1);
  }
  int get(int v, u8 c) const {
    for (int e = head[v]; e >= 0; e = e_next[e]) if (e_c[e] == c) return e_to[e];
    return -1;
  }
  void set(int v, u8 c, int to) {
    for (int e = head[v]; e >= 0; e = e_next[e]) if (e_c[e] == c) { e_to[e] = to; return; }
    e_next.push_back(head[v]); e_to.push_back(to); e_c.push_back(c); head[v] = (int)e_c.size() - 1;
  }
  int degree(int v) const { int d = 0; for (int e = head[v]; e >= 0; e = e_next[e]) ++d; return d; }
  int newstate(int l) { len.push_back(l); link.push_back(-1); head.push_back(-1); return (int)len.size() - 1; }
  // Extend with c. Returns M: length of the longest suffix (of the text before c) already
  // followed by c somewhere earlier; -1 if c never occurred.
  int extend(u8 c) {
    int cur = newstate(len[last] + 1);
    int p = last;
    while (p >= 0 && get(p, c) < 0) { set(p, c, cur); p = link[p]; }
    int M;
    if (p < 0) { link[cur] = 0; M = -1; }
    else {
      M = len[p];
      int q = get(p, c);
      if (len[p] + 1 == len[q]) link[cur] = q;
      else {
        int cl = newstate(len[p] + 1);
        for (int e = head[q]; e >= 0; e = e_next[e]) {
          e_next.push_back(head[cl]); e_to.push_back(e_to[e]); e_c.push_back(e_c[e]);
          head[cl] = (int)e_c.size() - 1;
        }
        link[cl] = link[q];
        while (p >= 0 && get(p, c) == q) { set(p, c, cl); p = link[p]; }
        link[q] = link[cur] = cl;
      }
    }
    last = cur;
    return M;
  }
};

// ---------------------------------------------------------------- hash predictors
static inline u64 mix64(u64 x) { x ^= x >> 31; x *= 0x7fb5d329728ea185ULL; x ^= x >> 27; x *= 0x81dadef4bc2dd44dULL; x ^= x >> 33; return x; }

struct KPred {           // most recent occurrence of the exact k-byte context
  int k; u32 bits; std::vector<u32> tab; // stores (position+1) of the byte that followed
  KPred(int k_, u32 bits_) : k(k_), bits(bits_), tab((size_t)1 << bits_, 0) {}
};

enum { P_K8, P_K16, P_K32, P_K64, P_LP, P_S16, P_S32, NP };
static const char* PNAME[NP] = {"k8", "k16", "k32", "k64", "lp", "skip16", "skip32"};

#pragma pack(push, 1)
struct PosRec {          // 24 bytes per coded position
  u16 L, M1;             // SAM: L (cap 65535), M+1 (0 = novel byte; cap 65535)
  u8 nconts, pad;
  u16 flags;             // bit 2p = predictor p valid, bit 2p+1 = predictor p correct
  u16 lp_len;            // verified length of lp's match (cap 65535)
  u8 lp_pred, k32_pred;
  u32 lp_dist, k16_dist, s16_dist;
};
#pragma pack(pop)

// distance buckets for the full-scale question "is the source inside the record's windows?"
// (fxcm match buffer 16 MiB, cmix Match history 60e6 bytes)
static int dbucket(u64 d) {
  if (d < (1u << 20)) return 0;          // < 1 MiB
  if (d < (16u << 20)) return 1;         // 1-16 MiB  (inside fxcm's buffer)
  if (d < 60000000ULL) return 2;         // 16 MiB-60 MB (only cmix Match models)
  if (d < 200000000ULL) return 3;        // 60-200 MB (beyond every CM match model)
  return 4;                              // > 200 MB
}
static const char* DBNAME[5] = {"<1M", "1M-16M", "16M-60M", "60M-200M", ">200M"};

// ---------------------------------------------------------------- final-stage mixer simulation
// A small online logistic mixer that re-predicts each bit from st(p_record) (+ bias) and,
// in the "with" variant, from the retrieval predictors' expected bits. The "base" variant has
// identical contexts/learning but no retrieval inputs, so (base - with) isolates the value of
// the retrieval information over and above recalibrating the record's own output.
static inline float stretchf(float p) { return logf(p / (1 - p)); }
static inline float squashf(float x) { if (x > 40) x = 40; if (x < -40) x = -40; return 1 / (1 + expf(-x)); }

struct Mixer {
  int n; float lr; std::vector<float> w;
  Mixer(int n_, int nctx, float lr_) : n(n_), lr(lr_), w((size_t)n_ * nctx, 0) {
    for (int c = 0; c < nctx; ++c) w[(size_t)c * n] = 1.0f;  // start as identity on st(p_rec)
  }
  float x[32]; float* wc = nullptr; float pr = 0.5f;
  float p(int ctx) { wc = &w[(size_t)ctx * n]; float s = 0; for (int i = 0; i < n; ++i) s += wc[i] * x[i]; pr = squashf(s); return pr; }
  void upd(int bit) { float err = (bit - pr) * lr; for (int i = 0; i < n; ++i) wc[i] += err * x[i]; }
};
struct Counter {   // adaptive probability that an expected bit is right, per context
  std::vector<u16> t; Counter(size_t n) : t(n, 49152) {}
  float p(size_t c) const { return (t[c] + 0.5f) / 65536.0f; }
  void upd(size_t c, int hit) { int v = t[c]; if (hit) v += (65535 - v) >> 5; else v -= v >> 5; t[c] = v; }
};
static int lenbucket(int l) {  // 0 = none
  if (l <= 0) return 0; if (l < 8) return 1; if (l < 12) return 2; if (l < 16) return 3; if (l < 24) return 4;
  if (l < 32) return 5; if (l < 64) return 6; if (l < 128) return 7; if (l < 512) return 8; return 9;
}

int main(int argc, char** argv) {
  bool use_sam = false, write_pos = true; u64 window = 10000000;
  int ai = 1;
  for (; ai < argc && argv[ai][0] == '-' && argv[ai][1] == '-'; ++ai) {
    if (!strcmp(argv[ai], "--sam")) use_sam = true;
    else if (!strcmp(argv[ai], "--no-pos")) write_pos = false;
    else if (!strcmp(argv[ai], "--window") && ai + 1 < argc) window = strtoull(argv[++ai], 0, 10);
    else die("unknown option");
  }
  if (argc - ai != 2) die("usage: a1_match [--sam] [--no-pos] [--window N] <costlog|stream> <outprefix>");
  std::string in = argv[ai], out = argv[ai + 1];

  // ---- load
  FILE* f = fopen(in.c_str(), "rb"); if (!f) die("cannot open input");
  fseeko(f, 0, SEEK_END); u64 fsz = ftello(f); fseeko(f, 0, SEEK_SET);
  char magic[8] = {0}; bool costlog = false; u32 recsz = 0;
  if (fsz >= 16) { fread(magic, 1, 8, f); costlog = !memcmp(magic, "FX2COST\0", 8); }
  std::vector<u8> s; std::vector<u16> pbits; std::vector<float> cost_rec;
  if (costlog) {
    u32 hdr[2]; fread(hdr, 4, 2, f); recsz = hdr[1];
    if (hdr[0] != 1 || recsz != 40) die("unsupported cost log version");
    u64 n = (fsz - 16) / recsz; s.resize(n); pbits.resize(n * 8); cost_rec.resize(n);
    std::vector<u8> rec(recsz);
    for (u64 i = 0; i < n; ++i) {
      if (fread(rec.data(), 1, recsz, f) != recsz) die("short read");
      s[i] = rec[0]; memcpy(&pbits[i * 8], &rec[8], 16); memcpy(&cost_rec[i], &rec[24], 4);
    }
  } else {
    fseeko(f, 0, SEEK_SET); s.resize(fsz);
    if (fread(s.data(), 1, fsz, f) != fsz) die("short read");
  }
  fclose(f);
  const u64 n = s.size();
  fprintf(stderr, "a1_match: %llu coded bytes (%s)\n", (unsigned long long)n, costlog ? "cost log" : "raw stream");

  // ---- predictors
  u32 hb = 22; while (hb < 28 && ((u64)1 << hb) < n) ++hb;  // table size ~ next pow2 >= n, 4M..256M
  std::vector<KPred> kp = {KPred(8, hb), KPred(16, hb), KPred(32, hb), KPred(64, hb)};
  std::vector<u32> skip16((size_t)1 << hb, 0), skip32((size_t)1 << hb, 0);
  const u64 B = 0x100000001b3ULL;  // rolling-hash base
  u64 Bk[65]; Bk[0] = 1; for (int i = 1; i <= 64; ++i) Bk[i] = Bk[i - 1] * B;
  u64 rh8 = 0, rh16 = 0, rh32 = 0, rh64 = 0;
  // normalized-token ring for skip-grams
  const int RING = 64; u64 tok[RING]; memset(tok, 0, sizeof(tok)); u64 ntok = 0;
  u64 th16 = 0, th32 = 0; int run_kind = 0; u32 run_off = 0;  // run_kind: 0 none, 1 digits, 2 name
  u64 lp_ptr = 0; u32 lp_len = 0;   // lp state: s[lp_ptr] is the predicted next byte

  SAM sam; if (use_sam) sam.reserve(n);

  // aggregates: [window][pred][dbucket][valid/correct]
  u64 nwin = (n + window - 1) / window;
  std::vector<u64> agg(nwin * NP * 5 * 2, 0), aggall(nwin * 3, 0);  // aggall: bytes, samL>=32, samM>=32
  std::vector<PosRec> pos; if (write_pos) pos.resize(n);

  // mixer sim (cost log only)
  const int NIN = 1 + 1 + NP;   // st(p_rec), bias, one per predictor
  const int NCTX = 10 * 4;      // lp length bucket x record-confidence bucket
  const float lr = getenv("A1_LR") ? atof(getenv("A1_LR")) : 0.002f;  // mixer learning rate
  Mixer mbase(2, NCTX, lr), mwith(NIN, NCTX, lr);
  const float degrade = getenv("A1_DEGRADE") ? atof(getenv("A1_DEGRADE")) : 1.0f;
  Counter conf((size_t)NP * 10 * 8 * 2);
  double cbase_tot = 0, cwith_tot = 0, crec_tot = 0;
  std::vector<float> mixout; if (costlog) mixout.resize(n * 2);

  for (u64 i = 0; i < n; ++i) {
    PosRec r; memset(&r, 0, sizeof(r));
    const u8 c = s[i];
    int pred[NP]; u64 dist[NP]; int plen[NP];
    for (int p = 0; p < NP; ++p) pred[p] = -1, dist[p] = 0, plen[p] = 0;

    // --- exact k-context predictors (context = s[i-k..i-1])
    u64 rh[4] = {rh8, rh16, rh32, rh64};
    for (int t = 0; t < 4; ++t) {
      int k = kp[t].k; if (i < (u64)k) continue;
      u32 slot = (u32)(mix64(rh[t]) >> (64 - hb));
      u32 j1 = kp[t].tab[slot];
      if (j1) {
        u64 j = j1 - 1;  // s[j] followed the stored context
        if (j >= (u64)k && !memcmp(&s[j - k], &s[i - k], k)) { pred[t] = s[j]; dist[t] = i - j; plen[t] = k; }
      }
    }
    // --- longest-match predictor
    {
      bool have = lp_len > 0;
      if (have && lp_len < 64) {  // try to upgrade to a longer verified candidate
        for (int t = 3; t >= 0; --t) if (pred[t] >= 0 && (u32)kp[t].k > lp_len) { lp_ptr = i - dist[t]; lp_len = kp[t].k; break; }
      }
      if (!have) {
        for (int t = 3; t >= 0; --t) if (pred[t] >= 0) { lp_ptr = i - dist[t]; lp_len = kp[t].k; break; }
        if (lp_len) {  // measure the true verified length (cap 4096)
          u32 l = lp_len; while (l < 4096 && lp_ptr > l && s[lp_ptr - 1 - l] == s[i - 1 - l]) ++l; lp_len = l;
        }
      }
      if (lp_len) { pred[P_LP] = s[lp_ptr]; dist[P_LP] = i - lp_ptr; plen[P_LP] = lp_len; }
    }
    // --- skip-gram predictors
    {
      u64 key16 = mix64(th16 ^ ((u64)run_kind << 56) ^ ((u64)run_off << 40) ^ 0x51);
      u64 key32 = mix64(th32 ^ ((u64)run_kind << 56) ^ ((u64)run_off << 40) ^ 0xA3);
      if (ntok >= 16) { u32 j1 = skip16[key16 >> (64 - hb)]; if (j1) { pred[P_S16] = s[j1 - 1]; dist[P_S16] = i - (j1 - 1); plen[P_S16] = 16; } }
      if (ntok >= 32) { u32 j1 = skip32[key32 >> (64 - hb)]; if (j1) { pred[P_S32] = s[j1 - 1]; dist[P_S32] = i - (j1 - 1); plen[P_S32] = 32; } }
      if (ntok >= 16) skip16[key16 >> (64 - hb)] = (u32)(i + 1);
      if (ntok >= 32) skip32[key32 >> (64 - hb)] = (u32)(i + 1);
    }

    // --- SAM
    if (use_sam) {
      int L = sam.link[sam.last] >= 0 ? sam.len[sam.link[sam.last]] : 0;
      int lk = sam.link[sam.last];
      r.nconts = (u8)std::min(255, lk >= 0 ? sam.degree(lk) : 0);
      int M = sam.extend(c);
      r.L = (u16)std::min(L, 65535); r.M1 = (u16)std::min(M + 1, 65535);
    }

    // --- record + aggregate
    u64 w = i / window;
    aggall[w * 3 + 0]++;
    if (use_sam) { if (r.L >= 32) aggall[w * 3 + 1]++; if (r.M1 >= 33) aggall[w * 3 + 2]++; }
    for (int p = 0; p < NP; ++p) if (pred[p] >= 0) {
      bool ok = pred[p] == c;
      r.flags |= 1u << (2 * p); if (ok) r.flags |= 1u << (2 * p + 1);
      u64* a = &agg[((w * NP + p) * 5 + dbucket(dist[p])) * 2];
      a[0]++; if (ok) a[1]++;
    }
    r.lp_len = (u16)std::min<u32>(lp_len, 65535); r.lp_pred = pred[P_LP] >= 0 ? pred[P_LP] : 0;
    r.k32_pred = pred[P_K32] >= 0 ? pred[P_K32] : 0;
    r.lp_dist = (u32)std::min<u64>(dist[P_LP], 0xffffffffu);
    r.k16_dist = (u32)std::min<u64>(dist[P_K16], 0xffffffffu);
    r.s16_dist = (u32)std::min<u64>(dist[P_S16], 0xffffffffu);
    if (write_pos) pos[i] = r;

    // --- mixer simulation over the 8 bits of this byte
    if (costlog) {
      double cb = 0, cw = 0, cr = 0;
      bool alive[NP]; for (int p = 0; p < NP; ++p) alive[p] = pred[p] >= 0;
      int lb = lenbucket(plen[P_LP]);
      int prefix = 1;
      for (int b = 7; b >= 0; --b) {
        int bit = (c >> b) & 1;
        float prec = pbits[i * 8 + (7 - b)] / 65536.0f;
        float st = stretchf(prec) * degrade;  // degrade < 1: sanity check that the sim can find signal
        int cb4 = fabsf(st) < 1 ? 0 : fabsf(st) < 3 ? 1 : fabsf(st) < 6 ? 2 : 3;
        int ctx = lb * 4 + cb4;
        mbase.x[0] = st; mbase.x[1] = 0.25f;
        mwith.x[0] = st; mwith.x[1] = 0.25f;
        size_t cidx[NP];
        for (int p = 0; p < NP; ++p) {
          float v = 0; cidx[p] = 0;
          if (alive[p]) {
            int eb = (pred[p] >> b) & 1;
            cidx[p] = (((size_t)p * 10 + lenbucket(plen[p])) * 8 + (7 - b)) * 2 + (cb4 >= 2);
            float q = conf.p(cidx[p]); v = (eb ? 1 : -1) * stretchf(q);
          }
          mwith.x[2 + p] = v;
        }
        float pb = mbase.p(ctx), pw = mwith.p(ctx);
        auto clampp = [](float p) { return std::min(std::max(p, 1.0f / 4096), 1 - 1.0f / 4096); };
        float pbc = clampp(pb), pwc = clampp(pw);
        cb -= log2(bit ? pbc : 1 - pbc); cw -= log2(bit ? pwc : 1 - pwc); cr -= log2(bit ? prec : 1 - prec);
        mbase.upd(bit); mwith.upd(bit);
        for (int p = 0; p < NP; ++p) if (alive[p]) {
          int eb = (pred[p] >> b) & 1; conf.upd(cidx[p], eb == bit);
          if (eb != bit) alive[p] = false;   // a byte prediction is dead after its first wrong bit
        }
        prefix = prefix * 2 + bit;
      }
      mixout[i * 2] = (float)cb; mixout[i * 2 + 1] = (float)cw;
      cbase_tot += cb; cwith_tot += cw; crec_tot += cr;
    }

    // --- update histories with c
    if (i + 1 >= 8)  { /* store after context known */ }
    {
      // store the position of c for each k-context ending at i-1 (context s[i-k..i-1] -> next byte s[i])
      for (int t = 0; t < 4; ++t) { int k = kp[t].k; if (i >= (u64)k) kp[t].tab[(u32)(mix64(rh[t]) >> (64 - hb))] = (u32)(i + 1); }
      // roll hashes to include c
      auto roll = [&](u64& h, int k) { h = h * B + (c + 1); if (i >= (u64)k) h -= (u64)(s[i - k] + 1) * Bk[k]; };
      roll(rh8, 8); roll(rh16, 16); roll(rh32, 32); roll(rh64, 64);
      // lp follow
      if (lp_len) { if (s[lp_ptr] == c) { ++lp_ptr; if (lp_len < 65535) ++lp_len; } else lp_len = 0; }
      // tokens: digit run -> 'D', name run (0x40/0x07 flag + following word bytes) -> 'N'
      bool is_digit = c >= '0' && c <= '9';
      bool is_wordb = (c >= 'a' && c <= 'z') || c >= 0x80;
      int kind;
      if (is_digit) kind = 1;
      else if (c == 0x40 || c == 0x07) kind = 2;
      else if (run_kind == 2 && is_wordb) kind = 2;
      else kind = 0;
      bool new_tok = !(kind != 0 && kind == run_kind && !(c == 0x40 || c == 0x07));
      if (new_tok) {
        u64 t = kind == 1 ? 0x1000 : kind == 2 ? 0x2000 : (u64)c;
        u64 old16 = ntok >= 16 ? tok[(ntok - 16) % RING] : 0, old32 = ntok >= 32 ? tok[(ntok - 32) % RING] : 0;
        tok[ntok % RING] = t + 1; ++ntok;
        th16 = th16 * B + (t + 1) - (ntok > 16 ? old16 * Bk[16] : 0);
        th32 = th32 * B + (t + 1) - (ntok > 32 ? old32 * Bk[32] : 0);
        run_off = 0;
      } else ++run_off;
      run_kind = kind;
      if (run_off > 255) run_off = 255;
    }
    if ((i & ((1u << 24) - 1)) == 0 && i) fprintf(stderr, "\r%.1f%%", 100.0 * i / n);
  }
  fprintf(stderr, "\n");

  if (write_pos) {
    FILE* o = fopen((out + ".pos").c_str(), "wb"); if (!o) die("cannot write .pos");
    fwrite(pos.data(), sizeof(PosRec), n, o); fclose(o);
  }
  if (costlog) {
    FILE* o = fopen((out + ".mix").c_str(), "wb"); if (!o) die("cannot write .mix");
    fwrite(mixout.data(), 4, mixout.size(), o); fclose(o);
    printf("mixer sim: record %.0f bits (%.0f B), base-mixer %.0f B, with-retrieval %.0f B, retrieval gain %.0f B (%.3f%%)\n",
           crec_tot, crec_tot / 8, cbase_tot / 8, cwith_tot / 8, (cbase_tot - cwith_tot) / 8,
           100.0 * (cbase_tot - cwith_tot) / cbase_tot);
  }
  FILE* o = fopen((out + ".tsv").c_str(), "w"); if (!o) die("cannot write .tsv");
  fprintf(o, "window\twin_start\tbytes\tsamL32\tsamM32\tpred\tdist\tvalid\tcorrect\n");
  for (u64 w = 0; w < nwin; ++w)
    for (int p = 0; p < NP; ++p)
      for (int d = 0; d < 5; ++d) {
        u64* a = &agg[((w * NP + p) * 5 + d) * 2];
        fprintf(o, "%llu\t%llu\t%llu\t%llu\t%llu\t%s\t%s\t%llu\t%llu\n", (unsigned long long)w,
                (unsigned long long)(w * window), (unsigned long long)aggall[w * 3],
                (unsigned long long)aggall[w * 3 + 1], (unsigned long long)aggall[w * 3 + 2], PNAME[p], DBNAME[d],
                (unsigned long long)a[0], (unsigned long long)a[1]);
      }
  fclose(o);
  return 0;
}
