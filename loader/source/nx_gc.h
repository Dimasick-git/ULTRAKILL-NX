#pragma once
#include <stdint.h>
#include <stddef.h>
#include <switch.h>
typedef struct { uintptr_t address, caller; uint32_t owner; } NxGcLockTrace;
int nx_gc_snapshot_collector(ThreadContext *context, unsigned *phase, NxGcLockTrace *trace);
int nxb_gc_bridge_init(unsigned abi);
int nxb_gc_begin(void);
int nxb_gc_pause(uintptr_t thread, uintptr_t *sp);
int nxb_gc_resume(uintptr_t thread);
const uintptr_t *nxb_gc_roots(uintptr_t thread, size_t *words);
void nxb_gc_end(void);
int nx_gc_backend_ready(void);
unsigned nx_gc_stop_count(void);
int nx_gc_snapshot_begin(void);
void nx_gc_snapshot_end(void);
