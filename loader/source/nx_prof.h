/* nx_prof.h -- сэмплирующий профайлер UnityMain/UnityGfxDeviceWorker (nx_prof.c). */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Запустить, если в RomFS/папке игры есть assets/NxPort/nx_prof.bin. */
void nx_prof_start(uintptr_t unity_base, size_t unity_size, uintptr_t il2cpp_base, size_t il2cpp_size);
