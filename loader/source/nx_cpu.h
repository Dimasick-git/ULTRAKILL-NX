#pragma once
#include <stddef.h>
#include <stdint.h>
void nx_cpu_init(void);
void nx_cpu_apply_config(void);   /* после read_config: core3 0 убирает ядро 3 */
uint32_t nx_cpu_allowed_mask(void);
unsigned nx_cpu_count(void);
void nx_cpu_thread_named(uint32_t handle, const char *name);
const char *nx_cpu_list(void);
const char *nx_cpu_info(void);
int nx_sched_getaffinity(int pid, size_t size, void *set);
int nx_sched_setaffinity(int pid, size_t size, const void *set);