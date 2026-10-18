#!/usr/bin/env python3
"""Run actual diagnostic stack walker against Windows no-access guard pages.

python tools/test_diag_snapshot.py --cc C:/path/to/gcc.exe
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PRE = r'''
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "diag_memory.h"
typedef unsigned u32;
typedef struct { uint64_t addr,size; unsigned perm; } MemoryInfo;
typedef struct { uint64_t x; } Reg;
typedef struct { Reg pc; uint64_t lr,sp,fp; Reg cpu_gprs[29]; } ThreadContext;
typedef struct { unsigned p_type,p_flags; uint64_t p_vaddr,p_filesz; } Elf64_Phdr;
typedef struct { char name[64]; void *load_virtbase; size_t load_size; int phnum; Elf64_Phdr phdr[3]; } so_module;
enum { Perm_R=1,Perm_X=4,PT_LOAD=1,PF_R=4,PF_X=1 };
#define R_SUCCEEDED(x) ((x)==0)
#define R_FAILED(x) ((x)!=0)
static so_module module;
static int g_scan_stack=1,query_mode,returns;
static uint64_t stack_base;
static int svcQueryMemory(MemoryInfo *out,u32 *page,uint64_t addr) {
  if (addr==stack_base && query_mode==1) return -1;
  MEMORY_BASIC_INFORMATION m;
  if (!VirtualQuery((void*)(uintptr_t)addr,&m,sizeof m)) return -1;
  out->addr=(uint64_t)(uintptr_t)m.BaseAddress;out->size=m.RegionSize;out->perm=0;
  if (m.State==MEM_COMMIT && !(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) {
    if (m.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)) out->perm|=Perm_R;
    if (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)) out->perm|=Perm_X;
  }
  if (addr==stack_base && query_mode==2) out->perm=0;
  if (addr==stack_base && query_mode==3) out->size=0;
  return 0;
}
static so_module *so_find_module_by_addr(const void *p) {
  uint64_t a=(uint64_t)(uintptr_t)p,b=(uint64_t)(uintptr_t)module.load_virtbase;
  return a>=b && a-b<module.load_size ? &module : NULL;
}
static void resolve_addr(char *buf,size_t n,uint64_t addr){snprintf(buf,n,"0x%llx",(unsigned long long)addr);}
static void dump_emit(const char *fmt,...) {if(strstr(fmt,"ret@"))returns++;}
'''
MAIN = r'''
int main(void) {
  SYSTEM_INFO si;GetSystemInfo(&si);size_t page=si.dwPageSize;
  uint8_t *code=VirtualAlloc(NULL,page*3,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  uint8_t *stack=VirtualAlloc(NULL,page*3,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  assert(code&&stack);DWORD old;
  *(uint32_t*)(code+page)=0x94000000; /* BL */
  assert(VirtualProtect(code,page,PAGE_NOACCESS,&old));
  assert(VirtualProtect(code+page,page,PAGE_EXECUTE_READ,&old));
  assert(VirtualProtect(code+page*2,page,PAGE_NOACCESS,&old));
  assert(VirtualProtect(stack,page,PAGE_NOACCESS,&old));
  assert(VirtualProtect(stack+page*2,page,PAGE_NOACCESS,&old));
  strcpy(module.name,"libunity.so");module.load_virtbase=code+page;module.load_size=page*2;
  module.phnum=2;
  module.phdr[0]=(Elf64_Phdr){PT_LOAD,PF_R|PF_X,0,page};
  /* Even misleading ELF permissions must not override kernel no-access. */
  module.phdr[1]=(Elf64_Phdr){PT_LOAD,PF_R|PF_X,page,page};
  uint64_t *slots=(uint64_t*)(stack+page);
  uint64_t base=(uint64_t)(uintptr_t)module.load_virtbase;
  slots[0]=base; /* r21 crash: v-4 lies in the preceding no-access page */
  slots[1]=base+4; /* real BL return site */
  slots[2]=base+page+4; /* within module range, no-access mapping */
  slots[3]=base+5; /* unaligned */
  ThreadContext ctx={0};ctx.sp=stack_base=(uint64_t)(uintptr_t)slots;
  ctx.fp=ctx.sp+page-8; /* last 8 bytes: frame record needs 16 */
  dump_thread_context("test",&ctx);
  assert(returns==1);
  for(query_mode=1;query_mode<=3;query_mode++) {
    returns=0;ctx.fp=1;dump_thread_context("no readable stack",&ctx);assert(!returns);
  }
  uint64_t site=0;
  assert(!diag_return_site(base,page,base,&site));
  assert(diag_return_site(base,page,base+page,&site)&&site==base+page-4);
  assert(!diag_return_site(base,page,base+page+4,&site));
  assert(!diag_span_contains(base,page,UINT64_MAX-7,16));
  assert(!diag_span_contains(base,page,base-8,16));
  assert(!diag_span_contains(base,page,base,0));
  VirtualFree(code,0,MEM_RELEASE);VirtualFree(stack,0,MEM_RELEASE);
  puts("PASS: actual snapshot walker skips module-base, unaligned/no-access code and invalid/unreadable stack; valid BL retained");
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='gcc')
    args=ap.parse_args()
    source=(ROOT/'loader/source/diag.c').read_text(encoding='utf-8')
    start=source.index('static void dump_thread_context(')
    end=source.index('/* Pause just long enough',start)
    with tempfile.TemporaryDirectory(prefix='nx-diag-test-') as temp:
        path=Path(temp);c=path/'test.c';exe=path/'test.exe'
        c.write_text(PRE+source[start:end]+MAIN,encoding='utf-8')
        subprocess.run([args.cc,'-std=gnu11','-O2','-I'+str(ROOT/'loader/source'),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=15)

if __name__=='__main__': main()
