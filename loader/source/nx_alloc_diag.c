/* Observe failed Android allocations without allocating/logging in the failure
 * path. Returning exactly the allocator result preserves the caller's contract.
 * The periodic engine diagnostic drains the fixed records on a live thread. */
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include "nx_alloc_diag.h"
#include "util.h"

typedef struct { unsigned state, kind; size_t size; uintptr_t caller; } Failure;
static Failure failures[16];
static unsigned failure_count;
static void record(unsigned kind, size_t size, uintptr_t caller) {
  unsigned id=__atomic_fetch_add(&failure_count,1,__ATOMIC_RELAXED);
  if(id>=16)return;
  failures[id].kind=kind;failures[id].size=size;failures[id].caller=caller;
  __atomic_store_n(&failures[id].state,1,__ATOMIC_RELEASE);
}
#define CALLER ((uintptr_t)__builtin_return_address(0))
void *nx_malloc_diag(size_t size) {
  void *p=malloc(size);if(!p&&size)record(0,size,CALLER);return p;
}
void *nx_calloc_diag(size_t count,size_t size) {
  void *p=calloc(count,size);
  if(!p&&count&&size)record(1,count>SIZE_MAX/size?SIZE_MAX:count*size,CALLER);
  return p;
}
void *nx_realloc_diag(void *old,size_t size) {
  void *p=realloc(old,size);if(!p&&size)record(2,size,CALLER);return p;
}
void *nx_memalign_diag(size_t alignment,size_t size) {
  void *p=memalign(alignment,size);if(!p&&size)record(3,size,CALLER);return p;
}
void nx_alloc_diagnostics(void) {
  int saved_errno=errno;
  struct mallinfo info=mallinfo();
  debugPrintf("[heap] arena=%u used=%u free=%u top=%u alloc_failures=%u\n",
    (unsigned)info.arena,(unsigned)info.uordblks,(unsigned)info.fordblks,
    (unsigned)info.keepcost,__atomic_load_n(&failure_count,__ATOMIC_RELAXED));
  static unsigned next;
  static const char *names[]={"malloc","calloc","realloc","memalign"};
  for(;next<16;next++) {
    Failure *f=&failures[next];
    if(!__atomic_load_n(&f->state,__ATOMIC_ACQUIRE))break;
    debugPrintf("[alloc-failed] %s bytes=%lu caller=%p\n",names[f->kind],
      (unsigned long)f->size,(void*)f->caller);
  }
  errno=saved_errno;
}
