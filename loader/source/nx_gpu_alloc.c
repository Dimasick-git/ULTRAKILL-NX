/* Record the actual Nouveau BO failure, including the separate heap budget. */
#include <switch.h>
#include <nouveau.h>
#include <malloc.h>
#include <errno.h>
#include "nx_bridge.h"
#include "util.h"

extern int __real_nouveau_bo_new(struct nouveau_device *, uint32_t, uint32_t,
    uint64_t, union nouveau_bo_config *, struct nouveau_bo **);
int __wrap_nouveau_bo_new(struct nouveau_device *device, uint32_t flags, uint32_t alignment,
    uint64_t bytes, union nouveau_bo_config *config, struct nouveau_bo **out) {
  int result = __real_nouveau_bo_new(device, flags, alignment, bytes, config, out);
  if (result) {
    int saved_errno = errno;
    nxb_render_fault_report(2);
    static unsigned failures;
    unsigned count = __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count == 64 || count == 1024) {
      struct mallinfo heap = mallinfo();
      u64 total = 0, used = 0;
      svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
      svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
      debugPrintf("[gpu-alloc] failure=%u bytes=%llu rc=%d flags=0x%x heap_free=%uKB app_free=%lluKB\n",
          count, (unsigned long long)bytes, result, flags, (unsigned)heap.fordblks >> 10,
          (unsigned long long)((total > used ? total-used : 0) >> 10));
    }
    errno = saved_errno;
  }
  return result;
}
