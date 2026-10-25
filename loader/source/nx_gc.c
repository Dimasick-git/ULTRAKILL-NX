/* Horizon stop-the-world bridge for the matching patched Boehm backend.
 * No allocation, stdio, or diagnostic-registry lock while mutators are paused. */
#include <switch.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <malloc.h>
#include "nx_gc.h"
#include "nx_frame.h"
#include "diag.h"

#define GC_SLOTS 128
typedef struct {
  uintptr_t thread;
  Handle handle;
  int paused;
  uintptr_t sp;
  ThreadContext context; /* All GP/NEON registers, including LR and packed pointers. */
} GcSlot;
static GcSlot slots[GC_SLOTS];
static Mutex pause_gate;
static int active, ready;
static unsigned stop_count;
static Mutex owner_gate;
Handle nx_gc_collector_handle;
NxGcLockTrace nx_gc_lock_trace;
static unsigned phase;
static int heap_locked;
static uint64_t stw_begin; /* длительность паузы GC -> [frame-wait], без журнала внутри паузы */
extern void nx_log_gc_enter(void), nx_log_gc_leave(void);

/* Do not resume any mutator to diagnose a collector stall. Only the collector
 * itself is sampled, and is running again before the caller formats output. */
int nx_gc_snapshot_collector(ThreadContext *context, unsigned *at_phase, NxGcLockTrace *trace) {
  if (!mutexTryLock(&owner_gate)) return 0;
  int okay = 0;
  Handle collector=__atomic_load_n(&nx_gc_collector_handle,__ATOMIC_RELAXED);
  if (active && collector && collector != threadGetCurHandle()) {
    if (!R_FAILED(svcSetThreadActivity(collector, ThreadActivity_Paused))) {
      okay = !R_FAILED(svcGetThreadContext3(context, collector));
      if(trace) {
        trace->address=__atomic_load_n(&nx_gc_lock_trace.address,__ATOMIC_RELAXED);
        trace->caller=__atomic_load_n(&nx_gc_lock_trace.caller,__ATOMIC_RELAXED);
        if(trace->address)trace->owner=__atomic_load_n((uint32_t *)trace->address,__ATOMIC_RELAXED);
      }
      svcSetThreadActivity(collector, ThreadActivity_Runnable);
    }
    *at_phase = __atomic_load_n(&phase, __ATOMIC_RELAXED);
  }
  mutexUnlock(&owner_gate);
  return okay;
}

int nxb_gc_bridge_init(unsigned abi) {
  int supported = abi == 1;
  __atomic_store_n(&ready, supported, __ATOMIC_RELEASE);
  return supported;
}
int nx_gc_backend_ready(void) { return __atomic_load_n(&ready, __ATOMIC_ACQUIRE); }
unsigned nx_gc_stop_count(void) { return __atomic_load_n(&stop_count, __ATOMIC_RELAXED); }
int nx_gc_snapshot_begin(void) { return mutexTryLock(&pause_gate); }
void nx_gc_snapshot_end(void) { mutexUnlock(&pause_gate); }

int nxb_gc_begin(void) {
  mutexLock(&pause_gate);
  stw_begin = armGetSystemTick();
  mutexLock(&owner_gate);
  __atomic_store_n(&nx_gc_collector_handle,threadGetCurHandle(),__ATOMIC_RELAXED);
  active = 1;
  __atomic_store_n(&phase, 1, __ATOMIC_RELAXED);
  mutexUnlock(&owner_gate);
  nx_log_gc_enter();
  /* Newlib malloc is shared with the guest and the SD logger. Suspending a
   * mutator while it owns that lock strands GC allocations behind a thread
   * which cannot run until GC completes. Acquire it BEFORE suspending anyone.
   * The linked libnx implementation is recursive, so GC may allocate itself. */
  __malloc_lock(NULL);
  heap_locked = 1;
  __atomic_store_n(&phase, 2, __ATOMIC_RELAXED);
  __atomic_fetch_add(&stop_count, 1, __ATOMIC_RELAXED);
  return 0;
}

static GcSlot *find(uintptr_t thread) {
  for (unsigned i = 0; i < GC_SLOTS; ++i)
    if (slots[i].paused && slots[i].thread == thread) return &slots[i];
  return NULL;
}

/* Used on failure too: never abort/log with unrelated threads left suspended. */
void nxb_gc_end(void) {
  if (!active) return;
  mutexLock(&owner_gate);
  for (unsigned i = 0; i < GC_SLOTS; ++i) {
    if (slots[i].paused) {
      svcSetThreadActivity(slots[i].handle, ThreadActivity_Runnable);
      slots[i].paused = 0;
    }
  }
  active = 0;
  __atomic_store_n(&nx_gc_collector_handle,0,__ATOMIC_RELAXED);
  mutexUnlock(&owner_gate);
  if (heap_locked) { heap_locked = 0; __malloc_unlock(NULL); }
  nx_frame_add_gc(armGetSystemTick() - stw_begin);
  /* Logging can acquire ordinary locks again only AFTER all mutators resume. */
  nx_log_gc_leave();
  mutexUnlock(&pause_gate);
}

int nxb_gc_pause(uintptr_t thread, uintptr_t *sp) {
  if (!active || !sp) return EINVAL;
  GcSlot *slot = find(thread);
  if (slot) { *sp = slot->sp; return 0; }
  unsigned i;
  for (i = 0; i < GC_SLOTS && slots[i].paused; ++i) {}
  Handle handle = diag_find_thread_handle(thread);
  if (i == GC_SLOTS || !handle || handle == threadGetCurHandle()) {
    nxb_gc_end(); return EINVAL;
  }
  slot = &slots[i];
  if (R_FAILED(svcSetThreadActivity(handle, ThreadActivity_Paused))) {
    nxb_gc_end(); return EINVAL;
  }
  slot->handle = handle; slot->thread = thread; slot->paused = 1;
  ThreadContext context = {0};
  MemoryInfo memory; u32 page;
  if (R_FAILED(svcGetThreadContext3(&context, handle)) || !context.sp ||
      R_FAILED(svcQueryMemory(&memory, &page, context.sp)) || !(memory.perm & Perm_R) ||
      context.sp < memory.addr || context.sp-memory.addr >= memory.size) {
    nxb_gc_end(); return EINVAL;
  }
  slot->context = context;
  slot->sp = context.sp;
  *sp = slot->sp;
  return 0;
}

int nxb_gc_resume(uintptr_t thread) {
  __atomic_store_n(&phase, 4, __ATOMIC_RELAXED);
  GcSlot *slot = find(thread);
  if (!active || !slot) { nxb_gc_end(); return EINVAL; }
  if (R_FAILED(svcSetThreadActivity(slot->handle, ThreadActivity_Runnable))) {
    nxb_gc_end(); return EINVAL;
  }
  slot->paused = 0;
  return 0;
}

const uintptr_t *nxb_gc_roots(uintptr_t thread, size_t *words) {
  __atomic_store_n(&phase, 3, __ATOMIC_RELAXED);
  GcSlot *slot = find(thread);
  _Static_assert(sizeof(ThreadContext) % sizeof(uintptr_t) == 0, "GC register context word alignment");
  *words = slot ? sizeof(ThreadContext)/sizeof(uintptr_t) : 0;
  return slot ? (const uintptr_t *)&slot->context : NULL;
}
