#!/usr/bin/env python3
"""Patch the matching exported Boehm POSIX backend to use Horizon STW callbacks.

Original copyright/license headers are retained. Normal Android keeps signals
when nxbridge callbacks are absent. Run after export, before compile-cpp.
"""
from pathlib import Path
import argparse

BRIDGE = r'''
/* Modified for ULTRAKILL NX by Dimasick-git, 2026: Horizon STW bridge ABI 1. */
#include <dlfcn.h>
#include <stdint.h>
static int (*nx_gc_begin_fn)(void);
static int (*nx_gc_pause_fn)(uintptr_t, uintptr_t *);
static int (*nx_gc_resume_fn)(uintptr_t);
static const uintptr_t *(*nx_gc_roots_fn)(uintptr_t, size_t *);
static void (*nx_gc_end_fn)(void);
static int nx_gc_bridge_state;
static void nx_gc_bridge_setup(void) {
  int (*init)(unsigned);
  if (nx_gc_bridge_state) return;
  nx_gc_bridge_state = -1;
  nx_gc_begin_fn = (int (*)(void))dlsym(RTLD_DEFAULT, "nxb_gc_begin");
  nx_gc_pause_fn = (int (*)(uintptr_t,uintptr_t *))dlsym(RTLD_DEFAULT, "nxb_gc_pause");
  nx_gc_resume_fn = (int (*)(uintptr_t))dlsym(RTLD_DEFAULT, "nxb_gc_resume");
  nx_gc_roots_fn = (const uintptr_t *(*)(uintptr_t,size_t *))dlsym(RTLD_DEFAULT, "nxb_gc_roots");
  nx_gc_end_fn = (void (*)(void))dlsym(RTLD_DEFAULT, "nxb_gc_end");
  init = (int (*)(unsigned))dlsym(RTLD_DEFAULT, "nxb_gc_bridge_init");
  if (init && nx_gc_begin_fn && nx_gc_pause_fn && nx_gc_resume_fn &&
      nx_gc_roots_fn && nx_gc_end_fn && init(1)) nx_gc_bridge_state = 1;
}
'''

def synchronous_ack(text):
    marker = 'NX_GC_SYNCHRONOUS_ACK'
    if marker in text:
        return text
    edits = [
        ('                  sem_post(&GC_suspend_ack_sem);',
         '                  /* Horizon pause is synchronous: no signal ACK semaphore. */'),
        ('                sem_post(&GC_suspend_ack_sem);',
         '                /* Horizon resume is synchronous too. */'),
        ('static void suspend_restart_barrier(int n_live_threads)\n{',
         'static void suspend_restart_barrier(int n_live_threads)\n{\n    /* NX_GC_SYNCHRONOUS_ACK: kernel callbacks already completed the operation. */\n    if (nx_gc_bridge_state == 1) return;'),
        ('\n                               int (*suspend_restart_all)(void))\n{',
         '\n                               int (*suspend_restart_all)(void))\n{\n    if (nx_gc_bridge_state == 1) return n_live_threads;'),
        ('\n                                          int (*suspend_restart_all)(void))\n{',
         '\n                                          int (*suspend_restart_all)(void))\n{\n    if (nx_gc_bridge_state == 1) return;'),
    ]
    for old, new in edits:
        if text.count(old) != 1:
            raise ValueError(f'Unexpected synchronous ACK anchor: {text.count(old)} {old}')
        text = text.replace(old, new, 1)
    return text

def patch(path):
    text=path.read_text(encoding='utf-8')
    if 'Horizon STW bridge ABI 1' in text:
        updated = synchronous_ack(text)
        if updated != text: path.write_text(updated, encoding='utf-8')
        print('Horizon synchronous ACK patch ready:',path);return
    replacements=[
        ('#include "private/pthread_support.h"', '#include "private/pthread_support.h"\n'+BRIDGE),
        ('              result = RAISE_SIGNAL(p, GC_sig_suspend);', r'''              if (nx_gc_bridge_state == 1) {
                uintptr_t sp = 0;
                result = nx_gc_pause_fn((uintptr_t)p->id, &sp);
                if (result == 0) {
                  AO_store((volatile AO_t *)&p->stop_info.stack_ptr, (AO_t)sp);
                  AO_store_release(&p->stop_info.last_stop_count, GC_stop_count);
                  sem_post(&GC_suspend_ack_sem);
                }
              } else result = RAISE_SIGNAL(p, GC_sig_suspend);'''),
        ('            result = RAISE_SIGNAL(p, GC_sig_thr_restart);', r'''            if (nx_gc_bridge_state == 1) {
              result = nx_gc_resume_fn((uintptr_t)p->id);
              if (result == 0 && GC_retry_signals) {
                AO_store_release(&p->stop_info.last_stop_count, (AO_t)((word)GC_stop_count | 1));
                sem_post(&GC_suspend_ack_sem);
              }
            } else result = RAISE_SIGNAL(p, GC_sig_thr_restart);'''),
        ('GC_INNER void GC_stop_world(void)\n{', '''GC_INNER void GC_stop_world(void)
{
  nx_gc_bridge_setup();
  if (nx_gc_bridge_state == 1 && nx_gc_begin_fn() != 0) ABORT("NX GC begin failed");'''),
        ('GC_INNER void GC_stop_init(void)\n{', '''GC_INNER void GC_stop_init(void)
{
  nx_gc_bridge_setup();'''),
        ('        GC_push_all_stack_sections(lo, hi, traced_stack_sect);', r'''        GC_push_all_stack_sections(lo, hi, traced_stack_sect);
        if (nx_gc_bridge_state == 1 && !THREAD_EQUAL(p->id, self)) {
          size_t words = 0;
          const uintptr_t *roots = nx_gc_roots_fn((uintptr_t)p->id, &words);
          if (roots && words) GC_push_all_stack((ptr_t)roots, (ptr_t)(roots+words));
        }'''),
        ('    n_live_threads = GC_restart_all();', '''    n_live_threads = GC_restart_all();
    if (nx_gc_bridge_state == 1) nx_gc_end_fn();'''),
    ]
    for old,new in replacements:
        if text.count(old)!=1: raise ValueError(f'Expected one source anchor, found {text.count(old)}: {old}')
        text=text.replace(old,new,1)
    backup=path.with_suffix(path.suffix+'.pre-nx-gc')
    if not backup.exists(): backup.write_bytes(path.read_bytes())
    path.write_text(synchronous_ack(text),encoding='utf-8')
    print('Patched:',path)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path,help='exported IL2CPP/external/bdwgc/pthread_stop_world.c')
    patch(ap.parse_args().source)
