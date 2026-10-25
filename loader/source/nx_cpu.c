/* Report actual process permissions; never invent access to the system core. */
#include <switch.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "nx_cpu.h"
#include "config.h"
#include "diag.h"
#include "util.h"

static uint32_t allowed;
static unsigned count;
static char list[32], info[256];

static void describe(void) {
  count = 0; size_t a = 0, b = 0;
  for (unsigned core = 0; core < 4; ++core) if (allowed & (1u << core)) {
    a += snprintf(list+a, sizeof(list)-a, "%s%u", count ? "," : "", core);
    b += snprintf(info+b, sizeof(info)-b, "processor\t: %u\n", core);
    ++count;
  }
  snprintf(list+a, sizeof(list)-a, "\n");
  snprintf(info+b, sizeof(info)-b, "Features\t: fp asimd aes pmull sha1 sha2 crc32\n"
           "CPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\n"
           "CPU part\t: 0xd07\nCPU revision\t: 1\n");
}
void nx_cpu_init(void) {
  u64 mask = 0; s32 preferred = -1;
  Result rc = svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  if (R_FAILED(rc)) {
    /* A thread's permitted mask is a conservative fallback, not all 4 cores. */
    rc = svcGetThreadCoreMask(&preferred, &mask, CUR_THREAD_HANDLE);
  }
  allowed = R_SUCCEEDED(rc) ? (uint32_t)(mask & 15u) : 0;
  if (!allowed) allowed = 1u << (svcGetCurrentProcessorNumber() & 3);
  describe();
  debugPrintf("[cpu] allowed_mask=0x%x available=%u core3=%s query_rc=0x%x\n",
              allowed, count, allowed & 8 ? "allowed" : "unavailable", rc);
}
/* Ядро 3 открывает NPDM форвардера (highest_cpu_id 3). Конфиг читается
 * позже nx_cpu_init, а Unity спрашивает число ядер ещё позже, при старте
 * движка, поэтому core3 0 успевает убрать ядро до того, как Unity создаст
 * рабочие потоки (их число = ядер - 1). */
void nx_cpu_apply_config(void) {
  if (config.core3 || !(allowed & 8)) return;
  allowed &= 7u;
  describe();
  debugPrintf("[cpu] core3 0 в config.txt: ядро 3 не используется, mask=0x%x available=%u\n", allowed, count);
}
uint32_t nx_cpu_allowed_mask(void) { return allowed; }
unsigned nx_cpu_count(void) { return count; }
void nx_cpu_thread_named(uint32_t handle,const char *name) {
  /* UnityMain is pinned to CPU 0. Foreground jobs/rendering can otherwise
   * migrate onto it despite idle application cores. Keep the main thread's
   * core available without requesting the restricted system core. */
  uint32_t workers=allowed & ~1u;
  if (!handle || !name || !workers) return;
  /* Приоритеты по ролям. Unity задаёт их через setpriority и
   * pthread_setschedparam, а у нас это заглушки, и все её рабочие потоки
   * создаются на 0x3b (UnityMain -- 0x2c). В Horizon меньшее число = выше
   * приоритет. Поднять фоновые потоки (например, до 0x2e) -- значит поставить
   * их над Job.Worker и потоком рендера, и ожидание задач UnityMain растёт.
   * Поэтому фон не трогаем (0x3b), а то, что держит
   * кадр -- воркеры задач, поток рендера и микшер звука -- поднимаем до 0x2c.
   * На ядро 0 они не попадают (маска ниже), главному потоку не мешают. */
  static const char *const frame_critical[] = { "Job.Worker ", "UnityGfxDeviceWorker", "FMOD mixer thread" };
  s32 was = -1;
  svcGetThreadPriority(&was, (Handle)handle);
  int raised = 0;
  for (unsigned i = 0; i < sizeof frame_critical / sizeof frame_critical[0]; i++) {
    if (strncmp(name, frame_critical[i], strlen(frame_critical[i]))) continue;
    const Result prc = svcSetThreadPriority((Handle)handle, 0x2C);
    debugPrintf("[cpu] priority name=%s 0x%x -> 0x2c rc=0x%x\n", name, (unsigned)was, prc);
    raised = 1;
    break;
  }
  if (!raised) debugPrintf("[cpu] priority name=%s 0x%x (без изменений)\n", name, (unsigned)was);
  /* Ядро 0 целиком главному потоку -- в бою он упирается в своё ядро
   * (70-90%), а FMOD, фоновые воркеры, BatchDeleteObjects и сборщик мусора
   * раньше могли садиться рядом. Загрузочные потоки (Loading.*) оставлены на
   * всех ядрах: на загрузках главный поток простаивает. */
  /* Главный поток сначала называется NX_UIMain (main.c, поток nativeRender)
   * и лишь потом Unity переименовывает его в UnityMain. Правило только для
   * «UnityMain» отпускало NX_UIMain на ядра 1-2, где он толкался с воркерами
   * и рендером, а ядро 0 пустовало. Закрепляем оба имени на ядре 0 явно. */
  /* В бою PhysicsFixedUpdate ждёт задачи физики на Job.Worker до половины
   * своего времени, а ядро 0 в это время простаивало бы, будь рабочим потокам
   * туда нельзя. Поэтому главный поток на 0x2b, выше всех потоков игры, а
   * Job.Worker (0x2c) можно и на ядро 0: пока главный поток ждёт, они берут
   * его ядро, а проснувшись, он сразу их вытесняет. На одном приоритете так
   * нельзя -- Horizon не вытесняет поток своего уровня, пока тот не уступит. */
  if (!strcmp(name,"UnityMain") || !strcmp(name,"NX_UIMain")) {
    const Result mrc = svcSetThreadCoreMask((Handle)handle, 0, 1);
    const Result prc = config.worker_core0 ? svcSetThreadPriority((Handle)handle, 0x2B) : 0;
    debugPrintf("[cpu] main pinned name=%s core=0 mask=0x1 rc=0x%x priority=0x%x rc=0x%x\n", name, mrc,
                config.worker_core0 ? 0x2b : (unsigned)was, prc);
    return;
  }
  if (!strncmp(name,"Loading.",8)) return;
  /* На ядре 3 работают системные потоки, многие выше нашего приоритета.
   * Поток рендера и звук там будут ждать системы -- им ядра 1-2. Рабочие
   * потоки задач и фон -- куда угодно (фон на 0x3b уступает системе сам). */
  uint32_t mask = workers;
  if (config.worker_core0 && !strncmp(name, "Job.Worker ", 11)) mask = allowed;
  else if ((!strcmp(name, "UnityGfxDeviceWorker") || !strncmp(name, "FMOD", 4)) && (allowed & 6u)) mask = allowed & 6u;
  Result rc=svcSetThreadCoreMask((Handle)handle,-1,mask);
  debugPrintf("[cpu] worker isolation name=%s mask=0x%x rc=0x%x\n",name,mask,rc);
}
const char *nx_cpu_list(void) { return list; }
const char *nx_cpu_info(void) { return info; }

static Handle target(int pid) {
  if (pid == 0) return CUR_THREAD_HANDLE;
  return diag_find_tid_handle(pid);
}
int nx_sched_getaffinity(int pid, size_t size, void *set) {
  if (!set) { errno = EFAULT; return -1; }
  if (size < sizeof(u64)) { errno = EINVAL; return -1; }
  Handle h = target(pid);
  if (!h) { errno = ESRCH; return -1; }
  s32 preferred; u64 mask = 0;
  Result rc = svcGetThreadCoreMask(&preferred, &mask, h);
  if (R_FAILED(rc)) { errno = ESRCH; return -1; }
  mask &= allowed;
  memset(set, 0, size); memcpy(set, &mask, sizeof(mask));
  return 0;
}
int nx_sched_setaffinity(int pid, size_t size, const void *set) {
  if (!set) { errno = EFAULT; return -1; }
  if (size < sizeof(u64)) { errno = EINVAL; return -1; }
  u64 mask; memcpy(&mask, set, sizeof(mask)); mask &= allowed;
  if (!mask) { errno = EINVAL; return -1; }
  Handle h = target(pid);
  if (!h) { errno = ESRCH; return -1; }
  if (R_FAILED(svcSetThreadCoreMask(h, -1, (u32)mask))) { errno = EPERM; return -1; }
  return 0;
}
