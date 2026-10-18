/* Bounded samples at the point of a managed null-reference throw, BEFORE the
 * stack is unwound. Never pause another thread or use watchdog dump storage. */
#include <switch.h>
#include <stdint.h>
#include <string.h>
#include "so_util.h"
#include "diag_memory.h"
#include "util.h"
void nxb_nullcheck_site(const char *file, int line) {
  static unsigned count;
  unsigned sample=__atomic_add_fetch(&count,1,__ATOMIC_RELAXED);
  if (sample>4 && sample!=64 && sample!=1024 && sample!=8192) return;
  const char *name=file ? file : "?", *p;
  if ((p=strrchr(name,'/'))) name=p+1;
  if ((p=strrchr(name,'\\'))) name=p+1;
  debugPrintf("[nullcheck] sample=%u source=%s:%d\n",sample,name,line);
}
void nxb_exception_trace(void) {
  static unsigned count;
  unsigned sample = __atomic_add_fetch(&count, 1, __ATOMIC_RELAXED);
  if (sample > 4 && sample != 64 && sample != 1024 && sample != 8192) return;
  uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
  MemoryInfo stack; u32 page;
  if (R_FAILED(svcQueryMemory(&stack, &page, sp)) || !(stack.perm & Perm_R) ||
      !diag_span_contains(stack.addr, stack.size, sp, 16)) return;
  uint64_t remaining = stack.size - (sp-stack.addr);
  uint64_t bytes = remaining < 0x1800 ? remaining : 0x1800;
  debugPrintf("[throw-trace] NullReference sample=%u tid=%lu; return-address candidates (not a proven call chain)\n",
              sample, (unsigned long)threadGetCurHandle());
  unsigned printed = 0;
  for (uintptr_t at=sp; at-sp+8<=bytes && printed<24; at+=8) {
    uintptr_t ra=*(const uintptr_t *)at;
    so_module *m=so_find_module_by_addr((void *)ra);
    if (!m) continue;
    uint64_t site=0; int executable=0;
    for (int i=0; i<m->phnum; ++i) {
      const Elf64_Phdr *p=&m->phdr[i];
      if (p->p_type!=PT_LOAD || (p->p_flags&(PF_R|PF_X))!=(PF_R|PF_X) ||
          p->p_vaddr>UINT64_MAX-(uintptr_t)m->load_virtbase) continue;
      if (diag_return_site((uintptr_t)m->load_virtbase+p->p_vaddr,p->p_filesz,ra,&site)) {
        executable=1;break;
      }
    }
    if (!executable) continue;
    MemoryInfo code;
    if (R_FAILED(svcQueryMemory(&code,&page,site)) ||
        (code.perm&(Perm_R|Perm_X))!=(Perm_R|Perm_X) ||
        !diag_span_contains(code.addr,code.size,site,4)) continue;
    uint32_t op=*(const uint32_t *)(uintptr_t)site;
    if ((op&0xfc000000u)!=0x94000000u && (op&0xfffffc1fu)!=0xd63f0000u) continue;
    debugPrintf("[throw-trace] candidate@+0x%lx %s+0x%lx\n",
                (unsigned long)(at-sp),m->name,(unsigned long)(ra-(uintptr_t)m->load_virtbase));
    ++printed;
  }
}
