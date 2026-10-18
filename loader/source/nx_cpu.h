#pragma once
#include <stddef.h>
#include <stdint.h>
void nx_cpu_init(void);
uint32_t nx_cpu_allowed_mask(void);
unsigned nx_cpu_count(void);
const char *nx_cpu_list(void);
const char *nx_cpu_info(void);
int nx_sched_getaffinity(int pid, size_t size, void *set);
int nx_sched_setaffinity(int pid, size_t size, const void *set);
