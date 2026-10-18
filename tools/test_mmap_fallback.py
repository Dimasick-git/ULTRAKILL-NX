#!/usr/bin/env python3
"""Exercise the actual fallback mmap functions, including Unity's trim sequence.

python tools/test_mmap_fallback.py --cc C:/path/to/gcc.exe
--source may point at an older libc_shim.c to reproduce the premature free.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREAMBLE = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define MMAP_PAGE 4096u
#define BIONIC_MAP_ANONYMOUS 0x20
typedef struct { int held; } Mutex;
static void mutexLock(Mutex *m) { assert(!m->held); m->held=1; }
static void mutexUnlock(Mutex *m) { assert(m->held); m->held=0; }
#define debugPrintf(...) ((void)0)
static void *maps[8192], *raws[8192];
static int freed[8192], maps_n;
static void *test_memalign(size_t alignment, size_t n) {
  void *raw = malloc(n + alignment);
  if (!raw) return NULL;
  void *p = (void *)(((uintptr_t)raw + alignment - 1) & ~(uintptr_t)(alignment - 1));
  maps[maps_n]=p; raws[maps_n]=raw; freed[maps_n++]=0;
  memset(p, 0xcc, n);
  return p;
}
/* Record a backing free without returning its memory to the host allocator.
 * This makes the regression deterministic and avoids dereferencing freed RAM. */
static void test_free(void *p) {
  for (int i=0; i<maps_n; i++) if (maps[i]==p) { freed[i]++; return; }
  free(p);
}
static long nx_pread(int fd, void *p, size_t n, long off) {
  (void)fd; (void)off;
  size_t got = n > 17 ? 17 : n;
  memset(p, 0x5a, got);
  return (long)got;
}
#define memalign test_memalign
#define free test_free
'''
TEST = r'''
#undef free
static int backing_freed(void *p) {
  for (int i=0; i<maps_n; i++) if (maps[i]==p) return freed[i];
  assert(0); return -1;
}
int main(void) {
  const size_t P=MMAP_PAGE;
  unsigned char *p=mmap_fallback(7*P, BIONIC_MAP_ANONYMOUS, -1, 0);
  assert(p && g_fb_n==1 && g_fb_bytes==7*P);
  for(size_t i=0;i<7*P;i++) assert(p[i]==0);
  memset(p+2*P, 0x6d, 3*P);
  assert(unmap_range(p, 2*P)==1);              /* Unity's leading alignment trim */
  assert(backing_freed(p)==0);                 /* old code frees the entire map here */
  assert(unmap_range(p+5*P, 2*P)==1);          /* trailing trim */
  assert(backing_freed(p)==0);
  assert(unmap_range(p, 2*P)==1);              /* duplicate trim is harmless */
  for(size_t i=2*P;i<5*P;i++) assert(p[i]==0x6d);
  assert(unmap_range(p+3*P, P)==1);            /* interior hole */
  assert(backing_freed(p)==0);
  assert(unmap_range(p+2*P, P)==1);
  assert(backing_freed(p)==0);
  assert(unmap_range(p+4*P, P)==1);
  assert(backing_freed(p)==1 && g_fb_n==0 && g_fb_bytes==0);
  p=mmap_fallback(P+13, 0, 5, 123);
  assert(p && g_fb_bytes==2*P);
  for(size_t i=0;i<17;i++) assert(p[i]==0x5a);
  for(size_t i=17;i<2*P;i++) assert(p[i]==0);
  assert(unmap_range(p, P+13)==1);
  assert(backing_freed(p)==1 && g_fb_n==0 && g_fb_bytes==0);
  /* Exhausting tracking slots must fail, never return an untracked map. */
  g_fb_n=MMAP_FALLBACK_MAX;
  assert(mmap_fallback(P, BIONIC_MAP_ANONYMOUS,-1,0)==NULL);
  g_fb_n=0;
  for(int i=0;i<maps_n;i++) free(raws[i]);
  puts("PASS: prefix/tail/interior/duplicate munmap, backing lifetime, rounded file map, registry exhaustion");
  return 0;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default='cc')
    ap.add_argument('--source', type=Path, default=ROOT / 'loader/source/libc_shim.c')
    a = ap.parse_args()
    source = a.source.read_text(encoding='utf-8')
    start = source.index('#define MMAP_FALLBACK_MAX')
    end = source.index('void *mmap_fake(', start)
    fragment = source[start:end]
    old = 'mmap_fallback_free(void *addr)' in fragment
    wrapper = ('static int unmap_range(void *p, size_t n) { (void)n; return mmap_fallback_free(p); }'
               if old else 'static int unmap_range(void *p, size_t n) { return mmap_fallback_free(p,n); }')
    with tempfile.TemporaryDirectory(prefix='nx-mmap-') as tmp:
        c = Path(tmp) / 'mmap.c'
        exe = Path(tmp) / 'mmap.exe'
        c.write_text(PREAMBLE + fragment + wrapper + TEST, encoding='utf-8')
        subprocess.run([a.cc, '-std=gnu11', '-O2', '-Wall', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == '__main__':
    main()
