#pragma once
/* bionic Dl_info: four pointers on AArch64. Symbol names may be unavailable for
 * stripped libraries; image name and base still permit native stack resolution. */
typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} NxDlInfo;
int nx_dladdr(const void *address, NxDlInfo *info);
