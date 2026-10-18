#!/usr/bin/env python3
"""Test pre-heap demand-memory setup and real commit bookkeeping with mocked SVCs."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PRE = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef unsigned Result;
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define CUR_PROCESS_HANDLE 0
#define InfoType_AliasRegionAddress 1
#define InfoType_AliasRegionSize 2
#define InfoType_SystemResourceSizeTotal 3
#define InfoType_HeapRegionAddress 4
#define MemType_Unmapped 0
#define MMAP_ARENA_ALIGN ((size_t)64*1024*1024)
#define MMAP_VIRT_RESERVE ((size_t)6144*1024*1024)
#define OVERCOMMIT_HEAP_MB 608
#define MAKERESULT(a,b) 1
#define Module_Libnx 1
#define LibnxError_HeapAllocFailed 1
#define MMAP_PAGE 4096
#define MB ((size_t)1024*1024)
typedef struct { u64 addr,size; int type; } MemoryInfo;
static u64 g_alias_base,g_alias_size,g_oc_sysres;
static int g_oc_hint_map,g_oc_hint_unmap,g_overcommit,g_oc_shrink_tried,g_oc_probe_tried;
static unsigned g_oc_alias_mb,g_oc_shrink_rc,g_oc_probe_rc,g_oc_heap_mb,g_oc_freed_mb;
static void *g_oc_win,*g_mmap_arena_base;
static unsigned long g_oc_win_addr;
static size_t g_mmap_arena_size;
static const u64 heap_base=0x2000000000ULL,alias_base=0x1000000000ULL;
static u64 sysres,alias_size,current_heap,last_heap;
static int hints,collision,heap_calls,map_calls,unmap_calls,shrink_error,map_error,unmap_error;
static int envIsSyscallHinted(int n) { (void)n; return hints; }
static Result svcGetInfo(u64 *out,int type,int h,int sub) {
 (void)h;(void)sub;
 *out= type==1?alias_base:type==2?alias_size:type==3?sysres:heap_base;
 return 0;
}
static Result svcQueryMemory(MemoryInfo *m,u32 *pi,u64 a) {
 (void)pi;(void)a;m->addr=alias_base;m->size=alias_size;m->type=collision;return 0;
}
static Result svcSetHeapSize(void **out,u64 size) {
 heap_calls++; if(shrink_error && heap_calls==1)return 9;
 last_heap=size;current_heap=size;*out=(void*)(uintptr_t)heap_base;return 0;
}
static Result svcMapPhysicalMemory(void *a,u64 n) {
 (void)a;(void)n;map_calls++;
 assert(current_heap < 3072*MB); // probe occurs only after physical heap release
 return map_error?7:0;
}
static Result svcUnmapPhysicalMemory(void *a,u64 n) {
 (void)a;(void)n;unmap_calls++;return unmap_error?8:0;
}
static void diagAbortWithResult(Result r) { (void)r;abort(); }
/* The actual commit function also uses these arena globals. */
static unsigned char committed[32];
static unsigned char *mmap_committed=committed,*mmap_arena;
static size_t g_committed_pages,g_commit_peak;
#define debugPrintf(...) ((void)0)
'''
TEST = r'''
static void reset(void) {
 hints=1;collision=0;sysres=32*MB;alias_size=64ULL*1024*MB;
 current_heap=3072*MB;last_heap=0;heap_calls=map_calls=unmap_calls=0;
 shrink_error=map_error=unmap_error=0;
 g_overcommit=g_oc_shrink_tried=g_oc_probe_tried=0;
 g_mmap_arena_base=NULL;g_mmap_arena_size=0;
}
int main(void) {
 void *out=NULL;size_t fake=0;
 const size_t prefix=7*MB+0x3000;
 void *addr=(void*)(uintptr_t)(heap_base+prefix);
 reset();sysres=0;
 assert(!overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));assert(heap_calls==0);
 reset();hints=0;
 assert(!overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));assert(heap_calls==0);
 reset();collision=1;
 assert(!overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));assert(heap_calls==0);
 reset();alias_size=4ULL*1024*MB;
 assert(!overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));assert(heap_calls==0);
 reset();
 assert(overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));
 assert(out==addr && fake==608*MB && g_overcommit && g_mmap_arena_size==MMAP_VIRT_RESERVE);
 assert(last_heap >= prefix+608*MB+160*MB && last_heap%0x200000==0);
 assert(heap_calls==1 && map_calls==1 && unmap_calls==1);
 assert(g_oc_freed_mb==(3072*MB-last_heap)/MB);
 reset();map_error=1;
 assert(!overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));
 assert(heap_calls==2 && last_heap==3072*MB && !g_overcommit);
 reset();shrink_error=1;
 assert(!overcommit_setup(addr,3072*MB-prefix,160*MB,&out,&fake));
 assert(heap_calls==1 && map_calls==0 && !g_overcommit);
 reset();current_heap=800*MB;memset(committed,0,sizeof committed);
 mmap_arena=(void*)(uintptr_t)alias_base;g_committed_pages=g_commit_peak=0;
 assert(arena_commit_locked(2,3));assert(g_committed_pages==3 && map_calls==1);
 assert(arena_commit_locked(2,3));assert(g_committed_pages==3 && map_calls==1);
 map_error=1;assert(!arena_commit_locked(8,4));
 assert(!committed[8] && g_committed_pages==3);
 map_error=0;assert(arena_commit_locked(8,4));assert(g_committed_pages==7);
 arena_decommit_locked(3,7);assert(g_committed_pages==3);
 assert(committed[2] && committed[10] && committed[11]);
 puts("PASS: capability gates, heap-prefix preservation, aligned shrink, probe rollback, lazy commit/recommit/decommit and failure propagation");
 return 0;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default='cc')
    a = ap.parse_args()
    main_c = (ROOT/'loader/source/main.c').read_text(encoding='utf-8')
    libc = (ROOT/'loader/source/libc_shim.c').read_text(encoding='utf-8')
    snippets = [function(main_c, 'static void *overcommit_reserve_window('),
                function(main_c, 'static int overcommit_setup('),
                function(libc, 'static int arena_commit_locked('),
                function(libc, 'static void arena_decommit_locked(')]
    with tempfile.TemporaryDirectory(prefix='nx-demand-') as tmp:
        c, exe = Path(tmp)/'demand.c', Path(tmp)/'demand.exe'
        c.write_text(PRE+'\n'.join(snippets)+TEST, encoding='utf-8')
        subprocess.run([a.cc,'-std=gnu11','-O2','-Wall',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30)


if __name__ == '__main__':
    main()
