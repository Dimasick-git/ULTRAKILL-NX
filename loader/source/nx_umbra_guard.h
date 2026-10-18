#ifndef NX_UMBRA_GUARD_H
#define NX_UMBRA_GUARD_H
#include <stddef.h>
typedef void *(*NxUmbraInit)(const void *, size_t, void *);
typedef int (*NxUmbraStatus)(const void *);
/* Preserve LoadUmbraTome's valid/invalid result. An initializer failure must
 * never be passed to the original status reader, which dereferences this. */
static void *nx_umbra_guard(const void *data, size_t size, NxUmbraInit init,
                             NxUmbraStatus status, int *reason) {
  *reason = 0;
  if (!data || !size) return NULL;
  void *tome = init(data, size, NULL);
  if (!tome) { *reason = -1; return NULL; }
  *reason = status(tome);
  return *reason ? NULL : tome;
}
#endif
