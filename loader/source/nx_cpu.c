/* Report actual process permissions; never invent access to the system core. */
#include <switch.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "nx_cpu.h"
#include "diag.h"
#include "util.h"

static uint32_t allowed;
static unsigned count;
static char list[32], info[256];

void nx_cpu_init(void) {
  u64 mask = 0; s32 preferred = -1;
  Result rc = svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  if (R_FAILED(rc)) {
    /* A thread's permitted mask is a conservative fallback, not all 4 cores. */
    rc = svcGetThreadCoreMask(&preferred, &mask, CUR_THREAD_HANDLE);
  }
  allowed = R_SUCCEEDED(rc) ? (uint32_t)(mask & 15u) : 0;
  if (!allowed) allowed = 1u << (svcGetCurrentProcessorNumber() & 3);
  count = 0; size_t a = 0, b = 0;
  for (unsigned core = 0; core < 4; ++core) if (allowed & (1u << core)) {
    a += snprintf(list+a, sizeof(list)-a, "%s%u", count ? "," : "", core);
    b += snprintf(info+b, sizeof(info)-b, "processor\t: %u\n", core);
    ++count;
  }
  snprintf(list+a, sizeof(list)-a, "\n");
  snprintf(info+b, sizeof(info)-b, "Features\t: fp asimd aes pmull sha1 sha2 crc32\n"
           "CPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\n"
           "CPU part\t: 0xd07\nCPU revision\t: 1\n");
  debugPrintf("[cpu] allowed_mask=0x%x available=%u core3=%s query_rc=0x%x\n",
              allowed, count, allowed & 8 ? "allowed" : "unavailable", rc);
}
uint32_t nx_cpu_allowed_mask(void) { return allowed; }
unsigned nx_cpu_count(void) { return count; }
const char *nx_cpu_list(void) { return list; }
const char *nx_cpu_info(void) { return info; }

static Handle target(int pid) {
  if (pid == 0) return CUR_THREAD_HANDLE;
  return diag_find_tid_handle(pid);
}
int nx_sched_getaffinity(int pid, size_t size, void *set) {
  if (!set) { errno = EFAULT; return -1; }
  if (size < sizeof(u64)) { errno = EINVAL; return -1; }
  Handle h = target(pid);
  if (!h) { errno = ESRCH; return -1; }
  s32 preferred; u64 mask = 0;
  Result rc = svcGetThreadCoreMask(&preferred, &mask, h);
  if (R_FAILED(rc)) { errno = ESRCH; return -1; }
  mask &= allowed;
  memset(set, 0, size); memcpy(set, &mask, sizeof(mask));
  return 0;
}
int nx_sched_setaffinity(int pid, size_t size, const void *set) {
  if (!set) { errno = EFAULT; return -1; }
  if (size < sizeof(u64)) { errno = EINVAL; return -1; }
  u64 mask; memcpy(&mask, set, sizeof(mask)); mask &= allowed;
  if (!mask) { errno = EINVAL; return -1; }
  Handle h = target(pid);
  if (!h) { errno = ESRCH; return -1; }
  if (R_FAILED(svcSetThreadCoreMask(h, -1, (u32)mask))) { errno = EPERM; return -1; }
  return 0;
}
