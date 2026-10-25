#pragma once
#include <stddef.h>
void *nx_malloc_diag(size_t size);
void *nx_calloc_diag(size_t count, size_t size);
void *nx_realloc_diag(void *old, size_t size);
void *nx_memalign_diag(size_t alignment, size_t size);
void nx_alloc_diagnostics(void);
