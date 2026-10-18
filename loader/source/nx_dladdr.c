#include <string.h>
#include "so_util.h"
#include "nx_dladdr.h"

int nx_dladdr(const void *address, NxDlInfo *info) {
  if (!info) return 0;
  memset(info, 0, sizeof(*info));
  if (!address) return 0;
  so_module *module = so_find_module_by_addr(address);
  if (!module) return 0;
  info->dli_fname = module->name;
  info->dli_fbase = module->load_virtbase;
  /* Internal IL2CPP methods are absent from dynsym. Leave the optional symbol
   * fields empty rather than invent names, or scan thousands of exports during
   * every stack walk. IL2CPP has its own code-registration table for methods. */
  return 1;
}
