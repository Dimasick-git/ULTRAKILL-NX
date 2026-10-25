/* nx_frame.c -- раскладка кадра главного потока по фазам player loop Unity.
 *
 * Профайлер nx_prof.c останавливает UnityMain 250 раз в секунду и сам съедает
 * FPS. Здесь замер изнутри: ExecutePlayerLoop вызывает каждую фазу
 * через указатель из общей таблицы колбэков (ldr x8,[sys,#0xc0]; ldr x8,[x8];
 * blr x8), и значения в этой таблице подменяются обёртками. На вызов -- два
 * чтения таймера, счётчики пишет только главный поток, без блокировок и без
 * журнала по ходу кадра. Код движка не правится. Таблицу фаз и слот GOT с
 * адресом таблицы колбэков даёт tools/make_frame_table.py (nx_frame_table.h).
 *
 * Ожидания главного потока (futex, nanosleep/usleep) относятся к текущей
 * фазе -- видно, где фаза работает, а где ждёт потоки задач или рендера.
 *
 * Раз в 5 с в журнал: время кадра по частям (загрузчик / движок / фазы),
 * суммы по группам фаз, самые дорогие фазы, ожидание GPU, паузы GC и интервал
 * показа.
 *
 * В проекте Physics.autoSyncTransforms = true, и каждый запрос к физике из
 * скриптов сначала переносит в неё сдвинутые трансформы -- в бою до 66 раз за
 * кадр, 3.5 мс. С physics_sync 1 автосинхронизация выключается, а перенос
 * делается один раз перед каждой фазой скриптов (FixedUpdate, Update, корутины,
 * LateUpdate) и, как всегда, перед шагом физики. Отличие от игры -- только
 * запрос, который в той же фазе идёт после сдвига трансформа другим скриптом. */
#include <switch.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "nx_frame.h"
#include "nx_frame_table.h"
#include "nx_offsets.h"
#include "nx_warp.h"
#include "util.h"

#define NX_FRAME_SLOTS  130
#define NX_FRAME_OUTSIDE NX_FRAME_SLOTS /* ожидания вне фаз player loop */
#define NX_FRAME_SCAN   0x1000          /* байт таблицы колбэков, где ищутся указатели */
#define NX_FRAME_TOP    12
#define NX_FRAME_WINDOW 5000000000ULL   /* нс */

typedef void (*loop_fn)(void);
static loop_fn g_orig[NX_FRAME_SLOTS];
static const char *g_name[NX_FRAME_SLOTS];
static uint64_t g_ticks[NX_FRAME_SLOTS], g_calls[NX_FRAME_SLOTS], g_max[NX_FRAME_SLOTS];
static uint64_t g_wait[NX_FRAME_SLOTS + 1];
static unsigned g_used;
static int g_physics = -1;
static int g_phase = NX_FRAME_OUTSIDE;
static void *g_main_tls;
static unsigned char g_sync_before[NX_FRAME_SLOTS];
static int g_sync_check = -1;            /* фаза, где раз в кадр проверяется, что автосинхронизация выкл. */
static void (*g_sync)(void);
static unsigned char (*g_autosync_get)(void);
static void (*g_autosync_set)(unsigned char);
static unsigned g_autosync_back;

static void physics_sync(unsigned i) {
  if ((int)i == g_sync_check && g_autosync_get()) {   /* кто-то включил обратно */
    g_autosync_set(0);
    g_autosync_back++;
  }
  g_sync();
}

static inline void account(unsigned i, uint64_t dt) {
  g_ticks[i] += dt;
  g_calls[i]++;
  if (dt > g_max[i]) g_max[i] = dt;
}

#define NXW(i) static void nxw_##i(void) { \
  const int prev = g_phase; g_phase = i; \
  const uint64_t t = armGetSystemTick(); if (g_sync_before[i]) physics_sync(i); \
  g_orig[i](); account(i, armGetSystemTick() - t); \
  g_phase = prev; }
#define NXW10(a) NXW(a##0) NXW(a##1) NXW(a##2) NXW(a##3) NXW(a##4) \
                 NXW(a##5) NXW(a##6) NXW(a##7) NXW(a##8) NXW(a##9)
NXW(0) NXW(1) NXW(2) NXW(3) NXW(4) NXW(5) NXW(6) NXW(7) NXW(8) NXW(9)
NXW10(1) NXW10(2) NXW10(3) NXW10(4) NXW10(5) NXW10(6)
NXW10(7) NXW10(8) NXW10(9) NXW10(10) NXW10(11) NXW10(12)

#define NXP(i) nxw_##i,
#define NXP10(a) NXP(a##0) NXP(a##1) NXP(a##2) NXP(a##3) NXP(a##4) \
                 NXP(a##5) NXP(a##6) NXP(a##7) NXP(a##8) NXP(a##9)
static const loop_fn g_wrap[NX_FRAME_SLOTS] = {
  NXP(0) NXP(1) NXP(2) NXP(3) NXP(4) NXP(5) NXP(6) NXP(7) NXP(8) NXP(9)
  NXP10(1) NXP10(2) NXP10(3) NXP10(4) NXP10(5) NXP10(6)
  NXP10(7) NXP10(8) NXP10(9) NXP10(10) NXP10(11) NXP10(12)
};

void nx_frame_install(uintptr_t ub, size_t usize) {
  if (!config.frame_stats) return;
  g_main_tls = armGetTls();   /* вызывается из цикла кадра, то есть с главного потока */
  const unsigned total = sizeof nx_frame_entries / sizeof nx_frame_entries[0];
  const uintptr_t table = *(volatile uintptr_t *)(ub + NX_FRAME_GOT);
  MemoryInfo mi; u32 pi;
  if (table < ub || table + NX_FRAME_SCAN > ub + usize || (table & 7) ||
      R_FAILED(svcQueryMemory(&mi, &pi, table)) || (mi.perm & Perm_Rw) != Perm_Rw ||
      table + NX_FRAME_SCAN > mi.addr + mi.size) {
    debugPrintf("[frame] таблица колбэков player loop не найдена (0x%lx) -- замер фаз выключен\n",
                (unsigned long)table);
    return;
  }
  for (size_t off = 0; off < NX_FRAME_SCAN && g_used < NX_FRAME_SLOTS; off += 8) {
    volatile uintptr_t *p = (volatile uintptr_t *)(table + off);
    const uintptr_t v = *p;
    if (v < ub || v >= ub + usize) continue;
    for (unsigned e = 0; e < total; e++) {
      if (v != ub + nx_frame_entries[e].off) continue;
      const unsigned i = g_used++;
      g_orig[i] = (loop_fn)v;
      g_name[i] = nx_frame_entries[e].name;
      if (!strcmp(g_name[i], "FixedUpdate.PhysicsFixedUpdate")) g_physics = (int)i;
      __atomic_store_n(p, (uintptr_t)g_wrap[i], __ATOMIC_RELEASE);
      break;
    }
  }
  debugPrintf("[frame] замер фаз кадра: таблица libunity+0x%lx, обёрнуто %u из %u фаз, build-id %s\n",
              (unsigned long)(table - ub), g_used, total, NX_FRAME_BUILD_ID);

  static const char *const sync_phases[] = {
    "FixedUpdate.ScriptRunBehaviourFixedUpdate", "Update.ScriptRunBehaviourUpdate",
    "Update.ScriptRunDelayedDynamicFrameRate", "PreLateUpdate.ScriptRunBehaviourLateUpdate",
  };
  const uint32_t sync = nxo_get("unity.physics_sync_transforms");
  const uint32_t get = nxo_get("unity.physics_get_autosync");
  const uint32_t set = nxo_get("unity.physics_set_autosync");
  if (!config.physics_sync) {
    debugPrintf("[frame] Physics.autoSyncTransforms: как в игре (physics_sync 0)\n");
    return;
  }
  if (!sync || !get || !set) {
    debugPrintf("[frame] нет unity.physics_sync_transforms/autosync в offsets.ini -- синхронизация как в игре\n");
    return;
  }
  unsigned found = 0;
  for (unsigned i = 0; i < g_used; i++)
    for (unsigned k = 0; k < sizeof sync_phases / sizeof sync_phases[0]; k++)
      if (!strcmp(g_name[i], sync_phases[k])) {
        g_sync_before[i] = 1;
        found++;
        if (k == 1) g_sync_check = (int)i;
      }
  if (found != sizeof sync_phases / sizeof sync_phases[0] || g_sync_check < 0) {
    memset(g_sync_before, 0, sizeof g_sync_before);
    g_sync_check = -1;
    debugPrintf("[frame] найдено %u из 4 фаз скриптов -- синхронизация как в игре\n", found);
    return;
  }
  g_sync = (void (*)(void))(ub + sync);
  g_autosync_get = (unsigned char (*)(void))(ub + get);
  g_autosync_set = (void (*)(unsigned char))(ub + set);
  const int was = g_autosync_get();
  g_autosync_set(0);
  debugPrintf("[frame] Physics.autoSyncTransforms %d -> %d, синхронизация перед 4 фазами скриптов\n",
              was, g_autosync_get());
}

/* --- ожидания главного потока --------------------------------------------- */
int nx_frame_on_main(void) { return g_main_tls && armGetTls() == g_main_tls; }
void nx_frame_main_wait(uint64_t ticks) { g_wait[g_phase] += ticks; nx_hook_main_wait(ticks); }

int nx_frame_nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!nx_frame_on_main()) return nanosleep(req, rem);
  const uint64_t t = armGetSystemTick();
  const int r = nanosleep(req, rem);
  nx_frame_main_wait(armGetSystemTick() - t);
  return r;
}
int nx_frame_usleep(unsigned int us) {   /* bionic: useconds_t = 32 бита */
  if (!nx_frame_on_main()) return usleep(us);
  const uint64_t t = armGetSystemTick();
  const int r = usleep(us);
  nx_frame_main_wait(armGetSystemTick() - t);
  return r;
}

/* --- ожидания с других потоков ------------------------------------------ */
static uint64_t g_fence_ticks, g_fence_calls, g_dequeue_ticks, g_gc_ticks, g_gc_count, g_gc_max;

Result __real_nvFenceWait(NvFence *f, s32 timeout_us);
Result __wrap_nvFenceWait(NvFence *f, s32 timeout_us) {
  const uint64_t t = armGetSystemTick();
  const Result rc = __real_nvFenceWait(f, timeout_us);
  __atomic_fetch_add(&g_fence_ticks, armGetSystemTick() - t, __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_fence_calls, 1, __ATOMIC_RELAXED);
  return rc;
}
void nx_frame_add_dequeue(uint64_t ticks) { __atomic_fetch_add(&g_dequeue_ticks, ticks, __ATOMIC_RELAXED); }
void nx_frame_add_gc(uint64_t ticks) {
  __atomic_fetch_add(&g_gc_ticks, ticks, __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_gc_count, 1, __ATOMIC_RELAXED);
  uint64_t was = __atomic_load_n(&g_gc_max, __ATOMIC_RELAXED);
  while (ticks > was && !__atomic_compare_exchange_n(&g_gc_max, &was, ticks, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

/* --- интервал показа -------------------------------------------------------
 * С интервалом 1 поток рендера при 45-55 FPS ждёт свободный буфер окна 5-7 мс
 * на кадр (nwindowDequeueBuffer, вертикальная развёртка), и главный поток в
 * FinishFrameRendering ждёт его вместе с ним. У окна три буфера, и libnx при
 * трёх и больше разрешает интервал 0:
 * кадр уходит компоновщику сразу, без ожидания развёртки.
 * swap_interval 2 (по умолчанию): интервал 1, пока игра держит 60 FPS (плавно,
 * без лишних кадров), и 0 при просадке; переключение по среднему интервалу
 * между показами с запасом, чтобы не дёргаться туда-обратно. Потолок 60 FPS
 * при интервале 0 держит targetFrameRate игры. */
static uint64_t g_present_last, g_present_frames, g_present_free, g_present_switches;
static double g_present_ema = 16.67;
double nx_frame_interval_ms(void) { return g_present_ema; }   /* среднее между показами (поток рендера) */
static int g_present_mode = -1;

void nx_frame_present(void *nwindow) {
  NWindow *nw = (NWindow *)nwindow;
  if (!nw) return;
  if (nx_warp_presenting()) {   /* вставленный кадр -- без ожидания развёртки, вне счёта */
    if (nw->swap_interval != 0) nwindowSetSwapInterval(nw, 0);
    return;
  }
  const uint64_t now = armGetSystemTick();
  if (g_present_last) {
    const double dt = (double)armTicksToNs(now - g_present_last) / 1e6;
    if (dt < 200.0) g_present_ema += (dt - g_present_ema) * 0.1;
  }
  g_present_last = now;
  int want;
  if (config.swap_interval == 0) want = 0;
  else if (config.swap_interval == 1) want = 1;
  /* лимит выше частоты экрана (или без лимита) -- развёртку не ждём;
   * при лимите 60 и ниже -- адаптивно, чтобы 60 шли ровно */
  else if (nx_fps_target() <= 0 || nx_fps_target() > 60) want = 0;
  else if (g_present_mode == 0) want = g_present_ema < 16.9 ? 1 : 0;
  else want = g_present_ema > 17.4 ? 0 : 1;
  if (want != g_present_mode || nw->swap_interval != (u32)want) {
    const Result rc = nwindowSetSwapInterval(nw, (u32)want);
    if (want != g_present_mode) {
      if (g_present_mode >= 0) __atomic_fetch_add(&g_present_switches, 1, __ATOMIC_RELAXED);
      else debugPrintf("[present] интервал показа %d (swap_interval %d) rc=0x%x\n", want, config.swap_interval, rc);
    }
    g_present_mode = R_SUCCEEDED(rc) ? want : 1;
  }
  __atomic_fetch_add(&g_present_frames, 1, __ATOMIC_RELAXED);
  if (g_present_mode == 0) __atomic_fetch_add(&g_present_free, 1, __ATOMIC_RELAXED);
}

/* --- кадр ------------------------------------------------------------------ */
static uint64_t t_loop, t_render, t_window, w_pre, w_render, w_frames;

static double ms(uint64_t ticks) { return (double)armTicksToNs(ticks) / 1e6; }

static void reset_window(uint64_t now) {
  t_window = now;
  w_pre = w_render = w_frames = 0;
  memset(g_ticks, 0, sizeof g_ticks);
  memset(g_calls, 0, sizeof g_calls);
  memset(g_max, 0, sizeof g_max);
  memset(g_wait, 0, sizeof g_wait);
}

static void report(uint64_t now) {
  const double frames = (double)w_frames;
  const double window = ms(now - t_window);
  const double frame = window / frames;
  uint64_t loop = 0, waited = 0;
  for (unsigned i = 0; i < g_used; i++) loop += g_ticks[i];
  for (unsigned i = 0; i <= NX_FRAME_SLOTS; i++) waited += g_wait[i];
  const double pre = ms(w_pre) / frames, render = ms(w_render) / frames, phases = ms(loop) / frames;
  const double steps = g_physics >= 0 ? (double)g_calls[g_physics] / frames : 0.0;
  debugPrintf("[frame] %.1f с, %lu кадров, %.1f FPS, кадр %.2f мс | загрузчик %.2f | движок %.2f (фазы %.2f) "
              "| после %.2f | физ. шагов %.2f | ожидание %.2f\n",
              window / 1000.0, (unsigned long)w_frames, frames * 1000.0 / window, frame, pre, render, phases,
              frame - pre - render, steps, ms(waited) / frames);

  if (config.engine_profiler) {
  static const char *const groups[] = { "TimeUpdate", "Initialization", "EarlyUpdate", "FixedUpdate",
                                        "PreUpdate", "Update", "PreLateUpdate", "PostLateUpdate" };
  char line[400]; size_t n = 0;
  for (unsigned g = 0; g < sizeof groups / sizeof groups[0]; g++) {
    const size_t len = strlen(groups[g]);
    uint64_t sum = 0, wsum = 0;
    for (unsigned i = 0; i < g_used; i++)
      if (!strncmp(g_name[i], groups[g], len) && g_name[i][len] == '.') { sum += g_ticks[i]; wsum += g_wait[i]; }
    n += snprintf(line + n, sizeof line - n, "%s%s %.2f/%.2f", g ? " | " : "", groups[g],
                  ms(sum) / frames, ms(wsum) / frames);
    if (n >= sizeof line) break;
  }
  debugPrintf("[frame-phase] %s (всего/из них ожидание)\n", line);

  unsigned order[NX_FRAME_SLOTS];
  for (unsigned i = 0; i < g_used; i++) order[i] = i;
  for (unsigned k = 0; k < NX_FRAME_TOP && k < g_used; k++) {
    unsigned best = k;
    for (unsigned j = k + 1; j < g_used; j++) if (g_ticks[order[j]] > g_ticks[order[best]]) best = j;
    const unsigned tmp = order[k]; order[k] = order[best]; order[best] = tmp;
    const unsigned i = order[k];
    if (ms(g_ticks[i]) / frames < 0.05) break;
    debugPrintf("[frame] %6.2f мс x%4.2f max %6.2f ожид %5.2f  %s\n", ms(g_ticks[i]) / frames,
                (double)g_calls[i] / frames, ms(g_max[i]), ms(g_wait[i]) / frames, g_name[i]);
  }
  nx_hook_report(frames);

  }

  if(config.reproject && !config.engine_profiler)nx_hook_report(frames);
  const uint64_t fence = __atomic_exchange_n(&g_fence_ticks, 0, __ATOMIC_RELAXED);
  const uint64_t fence_calls = __atomic_exchange_n(&g_fence_calls, 0, __ATOMIC_RELAXED);
  const uint64_t dequeue = __atomic_exchange_n(&g_dequeue_ticks, 0, __ATOMIC_RELAXED);
  const uint64_t gc = __atomic_exchange_n(&g_gc_ticks, 0, __ATOMIC_RELAXED);
  const uint64_t gc_count = __atomic_exchange_n(&g_gc_count, 0, __ATOMIC_RELAXED);
  const uint64_t gc_max = __atomic_exchange_n(&g_gc_max, 0, __ATOMIC_RELAXED);
  const uint64_t shown = __atomic_exchange_n(&g_present_frames, 0, __ATOMIC_RELAXED);
  const uint64_t free_shown = __atomic_exchange_n(&g_present_free, 0, __ATOMIC_RELAXED);
  const uint64_t switches = __atomic_exchange_n(&g_present_switches, 0, __ATOMIC_RELAXED);
  debugPrintf("[frame-wait] ожидание GPU %.2f мс/кадр (%.1f выз.), dequeue %.2f мс/кадр, "
              "паузы GC %lu шт %.1f мс (max %.1f) | показ без развёртки %.0f%%, переключений %lu "
              "| ожидание вне фаз %.2f\n",
              ms(fence) / frames, (double)fence_calls / frames, ms(dequeue) / frames,
              (unsigned long)gc_count, ms(gc), ms(gc_max),
              shown ? 100.0 * (double)free_shown / (double)shown : 0.0, (unsigned long)switches,
              ms(g_wait[NX_FRAME_OUTSIDE]) / frames);
}

void nx_frame_loop_begin(void) { t_loop = armGetSystemTick(); }
void nx_frame_render_begin(void) {
  t_render = armGetSystemTick();
  if (t_loop) w_pre += t_render - t_loop;
}
static uint64_t g_frames_total;
uint64_t nx_frame_number(void) { return g_frames_total; }
void nx_frame_render_end(void) {
  g_frames_total++;
  if (!config.frame_stats) return;
  const uint64_t now = armGetSystemTick();
  if (!t_window) { reset_window(now); return; }
  w_render += now - t_render;
  w_frames++;
  if (now - t_window < armNsToTicks(NX_FRAME_WINDOW)) return;
  report(now);
  reset_window(now);
}
