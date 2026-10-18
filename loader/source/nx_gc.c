/* Horizon stop-the-world bridge for the matching patched Boehm backend.
 * No allocation, stdio, or diagnostic-registry lock while mutators are paused. */
#include <switch.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include "nx_gc.h"
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
  active = 1;
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
  for (unsigned i = 0; i < GC_SLOTS; ++i) {
    if (slots[i].paused) {
      svcSetThreadActivity(slots[i].handle, ThreadActivity_Runnable);
      slots[i].paused = 0;
    }
  }
  active = 0;
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
  GcSlot *slot = find(thread);
  if (!active || !slot) { nxb_gc_end(); return EINVAL; }
  if (R_FAILED(svcSetThreadActivity(slot->handle, ThreadActivity_Runnable))) {
    nxb_gc_end(); return EINVAL;
  }
  slot->paused = 0;
  return 0;
}

const uintptr_t *nxb_gc_roots(uintptr_t thread, size_t *words) {
  GcSlot *slot = find(thread);
  _Static_assert(sizeof(ThreadContext) % sizeof(uintptr_t) == 0, "GC register context word alignment");
  *words = slot ? sizeof(ThreadContext)/sizeof(uintptr_t) : 0;
  return slot ? (const uintptr_t *)&slot->context : NULL;
}
