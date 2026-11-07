// stub.c: self-unpacking loader. The real executable is embedded (payload.bin, made by pack.c) and
// CM-decoded with the model in p1cm.h, written to an anonymous memfd and run with execveat(), keeping
// argv and envp. Fallback when memfd_create is unavailable: write ./.p1x and execve it.
// Freestanding: no libc, raw Linux x86-64 syscalls. Build with build_stub.sh.
#include "p1cm.h"

__asm__(".section .rodata\n.global payload\npayload:\n.incbin \"payload.bin\"\n.previous\n");
extern const U8 payload[];

static long sc(long n, long a, long b, long c, long d, long e) {
  long r;
  register long r10 __asm__("r10") = d, r8 __asm__("r8") = e;
  __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
  return r;
}

static U8 out[1 << 21];  // 2 MiB is enough for the executable (checked below)

__attribute__((used)) static void cmain(long *sp) {
  long argc = sp[0];
  char **argv = (char **)(sp + 1), **envp = argv + argc + 1;
  const U8 *ib = payload;
  U32 n = ib[0] | ib[1] << 8 | ib[2] << 16 | (U32)ib[3] << 24;
  if (n > sizeof out) sc(60, 3, 0, 0, 0, 0);
  ib += 4;
  cm_init(out);
  byte_ctx();
  U32 x1 = 0, x2 = 0xffffffffu, xd = 0;
  for (int k = 0; k < 4; k++) xd = (xd << 8) | *ib++;
  for (U32 i = 0; i < n * 8; i++) {
    U32 p = (U32)cm_predict();
    U32 xm = x1 + (U32)(((unsigned long long)(x2 - x1) * p) >> 12);
    int y = xd <= xm;
    if (y) x2 = xm; else x1 = xm + 1;
    cm_update(y);
    while (((x1 ^ x2) & 0xff000000u) == 0) { x1 <<= 8; x2 = (x2 << 8) | 255; xd = (xd << 8) | *ib++; }
  }
  xform(out, n, 0);
  long fd = sc(319, (long)"p1", 0, 0, 0, 0);  // memfd_create
  int mem = fd >= 0;
  if (!mem) fd = sc(2, (long)"./.p1x", 01101, 0700, 0, 0);  // open O_WRONLY|O_CREAT|O_TRUNC
  for (U32 o = 0; o < n;) {
    long w = sc(1, fd, (long)(out + o), n - o, 0, 0);
    if (w <= 0) sc(60, 4, 0, 0, 0, 0);
    o += (U32)w;
  }
  if (mem) sc(322, fd, (long)"", (long)argv, (long)envp, 0x1000);  // execveat(fd, "", AT_EMPTY_PATH)
  else { sc(3, fd, 0, 0, 0, 0); sc(59, (long)"./.p1x", (long)argv, (long)envp, 0, 0); }
  sc(60, 5, 0, 0, 0, 0);
}

__asm__(".text\n.global _start\n_start:\n mov %rsp, %rdi\n and $-16, %rsp\n call cmain\n hlt\n");

// gcc may turn the init loops into calls to these
void *memset(void *d, int c, unsigned long n) { U8 *p = d; while (n--) *p++ = (U8)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { U8 *p = d; const U8 *q = s; while (n--) *p++ = *q++; return d; }
