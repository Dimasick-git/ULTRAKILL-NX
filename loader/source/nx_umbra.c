#include <stdint.h>
#include <string.h>
#include "so_util.h"
#include "nx_offsets.h"
#include "nx_umbra_guard.h"
#include "util.h"

static NxUmbraInit init_tome;
static NxUmbraStatus tome_status;
static uintptr_t branch_target(uintptr_t pc) {
  uint32_t op=*(uint32_t *)pc;
  if ((op&0xfc000000u)!=0x94000000u) return 0;
  int64_t displacement=(int64_t)(op&0x03ffffffu);
  if (displacement&0x02000000) displacement-=0x04000000;
  return pc+displacement*4;
}
static void *load_tome(void *self, const void *data, size_t size) {
  (void)self;
  int reason;
  void *result = nx_umbra_guard(data, size, init_tome, tome_status, &reason);
  static unsigned reports;
  if (reason && __atomic_fetch_add(&reports, 1, __ATOMIC_RELAXED) < 8)
    debugPrintf("[umbra] rejected tome size=%lu reason=%d; no occlusion data for this load\n",
                (unsigned long)size, reason);
  return result;
}
void nx_umbra_install(so_module *mod) {
  uint32_t load = nxo_get("unity.umbra_load"), init = nxo_get("unity.umbra_init"),
           status = nxo_get("unity.umbra_status");
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  /* Signature for the matched Unity ARM64 body, checked before any code write.
   * Different engines need a new derivation, never a guessed offset. */
  const uint32_t expected[] = {0xd10203ff,0xa9074ffe,0xaa1f03f3};
  if (!load || !init || !status || mod->load_size < 0x60 ||
      load > mod->load_size-0x60 || init > mod->load_size-16 || status > mod->load_size-16 ||
      memcmp((void *)(base+load), expected, sizeof expected) ||
      *(uint32_t *)(base+status) != 0xb9400008u ||
      *(uint32_t *)(base+init) != 0xd100c3ffu ||
      branch_target(base+load+0x20) != base+init ||
      branch_target(base+load+0x28) != base+status) {
    debugPrintf("[umbra] guard skipped: missing offsets or unexpected engine signature\n");
    return;
  }
  init_tome = (NxUmbraInit)(base+init);
  tome_status = (NxUmbraStatus)(base+status);
  uintptr_t target = (uintptr_t)&load_tome;
  uint32_t stub[] = {0x58000050u,0xd61f0200u,(uint32_t)target,(uint32_t)(target>>32)};
  int rc = so_patch_code((void *)(base+load), stub, sizeof stub);
  debugPrintf("[umbra] null-safe LoadUmbraTome installed=%d at +0x%x\n", rc == 0, load);
}
