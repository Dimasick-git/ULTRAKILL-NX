#!/usr/bin/env python3
"""Compile the actual Horizon bridge with a deterministic kernel adapter.
Checks register-only roots, thread pause/resume, failure unwind and snapshot gate.
"""
import argparse
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
typedef unsigned Handle,u32;
typedef struct { int held; } Mutex;
static void mutexLock(Mutex*m){assert(!m->held);m->held=1;}
static void mutexUnlock(Mutex*m){assert(m->held);m->held=0;}
static int mutexTryLock(Mutex*m){if(m->held)return 0;m->held=1;return 1;}
typedef struct {uint64_t x;} Reg;
typedef struct {Reg cpu_gprs[29];uint64_t fp,lr,sp,vector_words[2];} ThreadContext;
typedef struct {uint64_t addr,size;unsigned perm;} MemoryInfo;
enum {ThreadActivity_Paused=1,ThreadActivity_Runnable=0,Perm_R=1};
#define R_FAILED(x) ((x)!=0)
static int paused[260],missing,fail_pause,fail_context,fail_query,no_read;
static int heap_depth,gc_logging;
static uint64_t ticks,gc_samples;
static uint64_t armGetSystemTick(void){ticks+=100;return ticks;}
static void nx_frame_add_gc(uint64_t elapsed){assert(elapsed==100&&!heap_depth&&gc_logging);for(int i=1;i<255;i++)assert(!paused[i]);++gc_samples;}
static void nx_log_gc_enter(void){assert(!gc_logging);gc_logging=1;}
static void nx_log_gc_leave(void){
  assert(gc_logging&&!heap_depth);
  for(int i=1;i<255;i++)assert(!paused[i]);
  gc_logging=0;
}
static void __malloc_lock(void *unused){(void)unused;assert(gc_logging&&!heap_depth);for(int i=1;i<255;i++)assert(!paused[i]);heap_depth=1;}
static void __malloc_unlock(void *unused){(void)unused;assert(heap_depth);heap_depth=0;}
static uintptr_t only_register_root=0x123456789abcULL;
static uint32_t diag_find_thread_handle(uintptr_t t){return t==missing?0:(uint32_t)t;}
static Handle current_handle=255;
static Handle threadGetCurHandle(void){return current_handle;}
static int svcSetThreadActivity(Handle h,int state){
  assert(h<260);if(h==fail_pause&&state==ThreadActivity_Paused)return -1;
  if(state==ThreadActivity_Paused)assert(heap_depth&&gc_logging);
  paused[h]=state==ThreadActivity_Paused;return 0;
}
static int svcGetThreadContext3(ThreadContext*c,Handle h){
  assert(paused[h]);if(h==fail_context)return -1;
  for(unsigned i=0;i<29;i++)c->cpu_gprs[i].x=i;
  c->cpu_gprs[19].x=only_register_root;c->fp=0x100800;c->sp=0x100000;
  c->lr=only_register_root+8;c->vector_words[0]=only_register_root+16;return 0;
}
static int svcQueryMemory(MemoryInfo*m,u32*p,uint64_t a){
  if(fail_query)return -1;m->addr=0x100000;m->size=0x1000;m->perm=no_read?0:Perm_R;return 0;
}
'''
MAIN=r'''
int main(void){
  size_t words=99;uintptr_t sp=0;
  assert(!nxb_gc_bridge_init(2)&&!nx_gc_backend_ready());
  assert(nxb_gc_bridge_init(1)&&nx_gc_backend_ready());
  assert(nxb_gc_pause(1,&sp)==EINVAL&&!paused[1]);
  assert(!nxb_gc_begin()&&!nx_gc_snapshot_begin());
  assert(!nxb_gc_pause(1,&sp)&&paused[1]&&sp==0x100000);
  ThreadContext sampled;unsigned sampled_phase=0;current_handle=254;
  assert(nx_gc_snapshot_collector(&sampled,&sampled_phase,NULL)&&sampled_phase==2);
  assert(paused[1]&&!paused[255]);
  fail_context=255;assert(!nx_gc_snapshot_collector(&sampled,&sampled_phase,NULL));
  assert(paused[1]&&!paused[255]);fail_context=0;current_handle=255;
  const uintptr_t *roots=nxb_gc_roots(1,&words);
  assert(roots&&words==sizeof(ThreadContext)/sizeof(uintptr_t)&&roots[19]==only_register_root&&roots[29]==0x100800);
  assert(roots[30]==only_register_root+8&&roots[32]==only_register_root+16);
  /* A duplicate stop reuses the same paused context. */
  assert(!nxb_gc_pause(1,&sp)&&paused[1]);
  assert(!nxb_gc_pause(2,&sp)&&paused[2]);
  assert(!nxb_gc_resume(1)&&!paused[1]&&paused[2]);
  assert(!nxb_gc_roots(1,&words)&&!words);
  assert(!nxb_gc_resume(2)&&!paused[2]);nxb_gc_end();
  assert(nx_gc_snapshot_begin());nx_gc_snapshot_end();
  assert(!heap_depth&&!gc_logging&&gc_samples==1);
  for(int failure=1;failure<=5;failure++){
    assert(!nxb_gc_begin());assert(!nxb_gc_pause(1,&sp));
    if(failure==1)missing=2;
    if(failure==2)fail_pause=2;
    if(failure==3)fail_context=2;
    if(failure==4)fail_query=1;
    if(failure==5)no_read=1;
    assert(nxb_gc_pause(2,&sp)==EINVAL);
    assert(!paused[1]&&!paused[2]&&!active);
    assert(nx_gc_snapshot_begin());nx_gc_snapshot_end();
    missing=fail_pause=fail_context=fail_query=no_read=0;
  }
  assert(!nxb_gc_begin());
  for(int i=1;i<=GC_SLOTS;i++)assert(!nxb_gc_pause(i,&sp));
  assert(nxb_gc_pause(GC_SLOTS+1,&sp)==EINVAL);
  for(int i=1;i<=GC_SLOTS;i++)assert(!paused[i]);
  assert(!active);nxb_gc_end();
  assert(nx_gc_snapshot_begin());nx_gc_snapshot_end();
  puts("PASS: actual GC bridge preserves register roots/SP, pauses/resumes mutators, excludes watchdog and unwinds every failure");
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    src=(ROOT/'loader/source/nx_gc.c').read_text()
    src='\n'.join(l for l in src.splitlines() if not l.startswith('#include'))
    hdr=(ROOT/'loader/source/nx_gc.h').read_text()
    hdr='\n'.join(l for l in hdr.splitlines() if not l.startswith('#include') and not l.startswith('#pragma'))
    with tempfile.TemporaryDirectory(prefix='nx-gc-test-') as temp:
        path=Path(temp);(path/'test.c').write_text(PRE+hdr+src+MAIN)
        subprocess.run([args.cc,'-O2','-UNDEBUG','-std=gnu11',str(path/'test.c'),'-o',str(path/'test.exe')],check=True)
        subprocess.run([str(path/'test.exe')],check=True,timeout=15)
if __name__=='__main__':main()
