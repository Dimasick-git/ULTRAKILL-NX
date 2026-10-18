#!/usr/bin/env python3
"""Run the loader's actual pthread shim on a POSIX host (or MinGW winpthreads).

python tools/test_pthread_sync.py --cc C:/path/to/gcc.exe
An intentionally slow calloc widens the lazy-initialization race. The once
initializer also pauses: every caller must see its fully published result.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREAMBLE = r'''
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
static void svcSleepThread(uint64_t ns) {
  struct timespec t = { ns / 1000000000, ns % 1000000000 };
  nanosleep(&t, NULL);
}
typedef struct { int held; } Mutex;
static void mutexLock(Mutex *m) {
  while (__atomic_exchange_n(&m->held, 1, __ATOMIC_ACQUIRE)) svcSleepThread(1000);
}
static void mutexUnlock(Mutex *m) { __atomic_store_n(&m->held, 0, __ATOMIC_RELEASE); }
#define DIAG_W_MUTEX 1
#define DIAG_W_COND 2
#define diag_wait_enter(k,p) ((void)0)
#define diag_wait_exit() ((void)0)
static void *slow_calloc(size_t n, size_t s) {
  svcSleepThread(1000000);
  return calloc(n, s);
}
#define calloc slow_calloc
'''
TEST = r'''
#undef calloc
#define THREADS 24
static pthread_barrier_t gate;
static volatile int once_control, nested_once;
static int initialized, init_calls, nested_calls, count;
static pthread_mutex_t *shared;
static pthread_cond_t *shared_cond;
static void nested_init(void) { nested_calls++; }
static void initialize(void) {
  init_calls++;
  assert(pthread_once_fake(&nested_once, nested_init) == 0);
  svcSleepThread(50000000);
  initialized = 12345;
}
static void *worker(void *arg) {
  (void)arg;
  pthread_barrier_wait(&gate);
  assert(pthread_once_fake(&once_control, initialize) == 0);
  assert(initialized == 12345);
  assert(ensure_cond(&shared_cond) == 0);
  for (int i = 0; i < 1000; i++) {
    assert(pthread_mutex_lock_fake(&shared) == 0);
    int n = count;
    if (i % 100 == 0) svcSleepThread(10000);
    count = n + 1;
    assert(pthread_mutex_unlock_fake(&shared) == 0);
  }
  return NULL;
}
int main(void) {
  pthread_t threads[THREADS];
  assert(pthread_barrier_init(&gate, NULL, THREADS) == 0);
  for (int i=0; i<THREADS; i++) assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
  for (int i=0; i<THREADS; i++) assert(pthread_join(threads[i], NULL) == 0);
  assert(init_calls == 1 && nested_calls == 1);
  assert(count == THREADS * 1000);
  assert(pthread_mutex_destroy_fake(&shared) == 0);
  assert(pthread_cond_destroy_fake(&shared_cond) == 0);
  shared = (pthread_mutex_t *)(uintptr_t)0x4000;
  assert(pthread_mutex_lock_fake(&shared) == 0);
  assert(pthread_mutex_lock_fake(&shared) == 0);
  assert(pthread_mutex_unlock_fake(&shared) == 0);
  assert(pthread_mutex_unlock_fake(&shared) == 0);
  assert(pthread_mutex_destroy_fake(&shared) == 0);
  shared = (pthread_mutex_t *)(uintptr_t)0x8000;
  assert(pthread_mutex_lock_fake(&shared) == 0);
  assert(pthread_mutex_lock_fake(&shared) == EDEADLK);
  assert(pthread_mutex_unlock_fake(&shared) == 0);
  assert(pthread_mutex_destroy_fake(&shared) == 0);
  puts("PASS: 24 concurrent once callers, nested once, 24000 protected updates, lazy cond, recursive/errorcheck mutex");
  return 0;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default='cc')
    ap.add_argument('--source', type=Path, default=ROOT / 'loader/source/imports.c')
    a = ap.parse_args()
    source = a.source.read_text(encoding='utf-8')
    start = source.index('int pthread_mutex_init_fake(')
    end = source.index('// bionic pthread_attr_t', start)
    fragment = source[start:end]
    with tempfile.TemporaryDirectory(prefix='nx-sync-') as tmp:
        c = Path(tmp) / 'sync.c'
        exe = Path(tmp) / 'sync.exe'
        c.write_text(PREAMBLE + '\nstatic Mutex g_sync_init_lock;\n' + fragment + TEST, encoding='utf-8')
        subprocess.run([a.cc, '-std=gnu11', '-O2', '-Wall', '-pthread', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == '__main__':
    main()
