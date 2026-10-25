/* Capture the collector's pending native mutex without logging or allocating.
 * The real lock implementation/ownership is unchanged. */
#include <switch.h>
#include <stdint.h>
#include "nx_gc.h"
extern Handle nx_gc_collector_handle;
extern NxGcLockTrace nx_gc_lock_trace;
extern void __real_mutexLock(Mutex *mutex);
void __wrap_mutexLock(Mutex *mutex) {
  Handle collector=__atomic_load_n(&nx_gc_collector_handle,__ATOMIC_RELAXED);
  int collecting=collector && collector==threadGetCurHandle();
  if(collecting) {
    __atomic_store_n(&nx_gc_lock_trace.address,(uintptr_t)mutex,__ATOMIC_RELAXED);
    __atomic_store_n(&nx_gc_lock_trace.caller,(uintptr_t)__builtin_return_address(0),__ATOMIC_RELAXED);
  }
  __real_mutexLock(mutex);
  if(collecting)__atomic_store_n(&nx_gc_lock_trace.address,0,__ATOMIC_RELAXED);
}
