/* main.c -- ULTRAKILL NX: точка входа загрузчика.
 *
 * Загрузчик -- из линии so-loader портов Unity-игр (см. CREDITS.md). Адреса
 * движка и игры -- в offsets.ini (tools/unity_syms.py, tools/derive_offsets.py).
 *
 * Unity 2022.3 / IL2CPP. Loads libmain + libunity + libil2cpp, then drives the
 * lifecycle the Java UnityPlayer normally runs (initJni -> recreate GFX state ->
 * render loop), calling the native entry points libunity.so registers from its
 * JNI_OnLoad (see unity_entrypoints.h). The engine owns its own EGL/GLES3 context
 * created from android_native_window(); SDL is audio/HID only.
 *
 * Heap + syscall scaffolding adapted from cr3_nx's main.c (MIT).
 */

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>
#include <SDL2/SDL.h>

#include "config.h"
#include "nx_gameroot.h"
#include "nx_assets.h"
#include "nx_offsets.h"
#include "nx_bridge.h"
#include "nx_video.h"
#include "nx_gc.h"
#include "nx_cpu.h"
#include "nx_prof.h"
#include "nx_frame.h"
#include "nx_warp.h"
#include "nx_salty.h"
#include "nx_fps_policy.h"
#include "nx_loading.h"
#include <dirent.h>
#include <strings.h>  /* strncasecmp */
#include "util.h"
#include "error.h"
#include "so_util.h"
extern void nx_umbra_install(so_module *mod);
#include "imports.h"
#include "jni_fake.h"
#include "android_native_unity.h"
#include "opensles.h"
#include "unity_entrypoints.h"
#include "diag.h"

/* DATA_ROOT is now a FUNCTION, not a macro: the game folder is resolved at
 * runtime from the .nro's own location so the port runs from any folder under
 * sdmc:/switch. See nx_gameroot.c.
 * The old macro spelling is kept as a call so the many use sites below read
 * unchanged -- but note it can no longer be string-concatenated with a literal;
 * every such site was converted to snprintf. */
#define DATA_ROOT  (game_root())
#define LIB_MAIN   "libmain.so"
#define LIB_UNITY  "libunity.so"
#define LIB_IL2CPP "libil2cpp.so"

void unity_environment_init(const char *data_root);   /* unity_glue.c */

static void *heap_so_base = NULL;
static size_t heap_so_limit = 0;

/* mmap arena. In overcommit mode (g_overcommit) this is a big *virtual* window in
 * the alias region: Unity's PROT_NONE pool reservations cost only address space
 * and physical pages are committed on demand via svcMapPhysicalMemory. In the
 * fallback path it's a fully heap-backed 256MB-aligned slab (the Switch has no
 * native overcommit). Consumed by mmap_fake/munmap_fake (libc_shim.c). */
void  *g_mmap_arena_base = NULL;
size_t g_mmap_arena_size = 0;
int    g_overcommit      = 0;          /* 1 = alias-region on-demand commit */
u64    g_alias_base = 0, g_alias_size = 0;
/* captured in __libnx_initheap for logging from main() (log file isn't open yet) */
unsigned g_oc_heap_mb = 0, g_oc_freed_mb = 0;
/* granular setup diagnostics so a failed gate tells us WHICH step bailed */
int      g_oc_hint_map = 0, g_oc_hint_unmap = 0;
unsigned g_oc_alias_mb = 0;
void    *g_oc_win = NULL;
int      g_oc_probe_tried = 0, g_oc_shrink_tried = 0;
/* stack-region overcommit arena armer (libc_shim.c) */
extern int oc_arena_init(void *window, size_t window_bytes, void *pool, size_t pool_bytes);
unsigned g_oc_probe_rc = 0, g_oc_shrink_rc = 0;
unsigned long g_oc_win_addr = 0;
u64      g_oc_sysres = 0;   /* system resource size (0 => svcMapPhysicalMemory unusable) */

so_module main_mod, unity_mod, il2cpp_mod;

/* defined in libc_shim.c; consumed by the GC stop-the-world bridge there */
extern uintptr_t g_il2cpp_base;

/* Replacement icall for UnityEngine.Application::get_internetReachability.
 * Returns NetworkReachability.NotReachable (0). See the frame-0 install site
 * for why (unblocks FirebaseManager.IsGetMessage / the boot coroutine). The
 * il2cpp icall ABI for this static getter is "int32_t func(MethodInfo*)"; we
 * ignore the hidden arg and just report no network. */


/* Unity's native time base is frozen in our environment (it never advances the
 * engine clock -- likely it expects Android Choreographer frame timestamps we
 * don't deliver). Every managed Time.* accessor therefore reads a frozen value:
 * deltaTime==0, time/realtimeSinceStartup constant. That freezes all time-based
 * game logic -- DOTween (the boot logo fade), WaitForSeconds, etc. -- which is
 * what holds the black screen (the fade never completes, so boot never starts).
 * Work around it by driving our own monotonic frame clock and redirecting the
 * managed Time accessors to it. nx_time_tick() runs once per render-loop frame. */
static volatile float  g_unity_dt   = 1.0f / 60.0f;
static volatile double g_unity_time = 0.0;
static volatile uint32_t g_frame_count = 0;   /* Time.frameCount source */
static uint64_t g_time_prev_ns  = 0;
static uint64_t g_time_start_ns = 0;
static uint64_t nx_now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static void nx_time_tick(void) {
  uint64_t now = nx_now_ns();
  g_frame_count++;                    /* advance Time.frameCount once per frame */
  if (!g_time_start_ns) g_time_start_ns = now;
  if (g_time_prev_ns) {
    double dt = (double)(now - g_time_prev_ns) / 1e9;
    if (dt < 0) dt = 0;
    /* тот же потолок, что у Time.maximumDeltaTime (physics_max_dt): Unity режет
     * deltaTime им же, и Update не должен убегать вперёд физики */
    const double cap = config.physics_max_dt > 0.0f ? config.physics_max_dt : 0.1;
    if (dt > cap) dt = cap;
    g_unity_dt = (float)dt;
    g_unity_time += dt;
  }
  g_time_prev_ns = now;
}
/* deltaTime обязан учитывать Time.timeScale: ULTRAKILL держит на нём стоп-кадры
 * при ударах и замедление парирования. Шкалу читаем у самого движка через icall
 * (сеттер timeScale в движке работает, заморожены только часы). */
static float (*g_get_timescale)(void) = NULL;
static float (*g_get_native_delta)(void) = NULL;
static float (*g_get_fixed_delta)(void) = NULL;
static void nx_time_bind_timescale(so_module *il2cpp) {
  typedef void *(*fn_resolve)(const char *);
  fn_resolve resolve = (fn_resolve)so_try_find_addr_rx(il2cpp, "il2cpp_resolve_icall");
  if (resolve)
    g_get_timescale = (float (*)(void))resolve("UnityEngine.Time::get_timeScale()");
  if (resolve) {
    g_get_native_delta = (float (*)(void))resolve("UnityEngine.Time::get_deltaTime()");
    g_get_fixed_delta = (float (*)(void))resolve("UnityEngine.Time::get_fixedDeltaTime()");
  }
  debugPrintf("[time] Time.get_timeScale icall = %p\n", (void *)g_get_timescale);
}
static float nx_delta_time(void) {
  if (g_get_native_delta) return g_get_native_delta();
  const float s = g_get_timescale ? g_get_timescale() : 1.0f;
  return g_unity_dt * (s < 0.0f ? 0.0f : s);
}
/* Диагностика «игра идёт, а экрана нет»: раз в 300 кадров -- часы самого движка
 * (icall'ы, мимо наших подмен), шкала времени и активная сцена. По ним видно,
 * стоят ли часы и на какой сцене игра застряла. */
static void *nx_time_manager(void);              /* часы движка, ниже */
static void *volatile g_tm;
static volatile uint32_t g_clock_thread_ticks;
static float (*g_icall_time)(void), (*g_icall_utime)(void);
static void  (*g_icall_active_scene)(int *);
static void *(*g_icall_scene_name)(int);
static int   (*g_icall_scene_count)(void);
static int64_t (*g_gc_used)(void), (*g_gc_heap)(void);
static int (*g_gc_disabled)(void);

/* Потолок шагов физики: Time.maximumDeltaTime = config.physics_max_dt.
 * Игра его не меняет (проверено по IL), движковый -- 0.1 с из TimeManager. */
static void  (*g_icall_set_max_dt)(float);
static float (*g_icall_get_max_dt)(void), (*g_icall_get_fixed_dt)(void);
static void nx_apply_physics_cap(const char *why) {
  if (!g_icall_set_max_dt || !g_icall_get_max_dt) return;
  const float want = config.physics_max_dt;
  const float was = g_icall_get_max_dt();
  if (was > want - 0.0005f && was < want + 0.0005f) return;
  g_icall_set_max_dt(want);
  const float fixed = g_icall_get_fixed_dt ? g_icall_get_fixed_dt() : 0.0f;
  debugPrintf("[physics] maximumDeltaTime %.4f -> %.4f (%s); fixedDeltaTime %.4f -> до %.1f шагов физики на кадр\n",
              was, g_icall_get_max_dt(), why, fixed, fixed > 0.0f ? want / fixed : 0.0f);
}

static void nx_diag_bind(so_module *il2cpp) {
  g_gc_used = (int64_t (*)(void))so_try_find_addr_rx(il2cpp, "il2cpp_gc_get_used_size");
  g_gc_heap = (int64_t (*)(void))so_try_find_addr_rx(il2cpp, "il2cpp_gc_get_heap_size");
  g_gc_disabled = (int (*)(void))so_try_find_addr_rx(il2cpp, "il2cpp_gc_is_disabled");
  typedef void *(*fn_resolve)(const char *);
  fn_resolve r = (fn_resolve)so_try_find_addr_rx(il2cpp, "il2cpp_resolve_icall");
  if (!r) return;
  g_icall_time  = (float (*)(void))r("UnityEngine.Time::get_time()");
  g_icall_utime = (float (*)(void))r("UnityEngine.Time::get_unscaledTime()");
  g_icall_active_scene = (void (*)(int *))r(
      "UnityEngine.SceneManagement.SceneManager::GetActiveScene_Injected(UnityEngine.SceneManagement.Scene&)");
  g_icall_scene_name  = (void *(*)(int))r("UnityEngine.SceneManagement.Scene::GetNameInternal(System.Int32)");
  g_icall_scene_count = (int (*)(void))r("UnityEngine.SceneManagement.SceneManager::get_sceneCount()");
  debugPrintf("[diag] icall: time=%p unscaled=%p scene=%p name=%p count=%p\n",
              (void *)g_icall_time, (void *)g_icall_utime, (void *)g_icall_active_scene,
              (void *)g_icall_scene_name, (void *)g_icall_scene_count);
  g_icall_set_max_dt   = (void (*)(float))r("UnityEngine.Time::set_maximumDeltaTime(System.Single)");
  g_icall_get_max_dt   = (float (*)(void))r("UnityEngine.Time::get_maximumDeltaTime()");
  g_icall_get_fixed_dt = (float (*)(void))r("UnityEngine.Time::get_fixedDeltaTime()");
  if (!g_icall_set_max_dt || !g_icall_get_max_dt)
    debugPrintf("[physics] icall maximumDeltaTime не найден (set=%p get=%p) -- потолок не ставлю\n",
                (void *)g_icall_set_max_dt, (void *)g_icall_get_max_dt);
  nx_apply_physics_cap("старт");
}
static void nx_diag_tick(int frame) {
  extern void nx_alloc_diagnostics(void);
  if(config.engine_profiler) { nx_alloc_diagnostics(); opensles_diagnostics(); }
  if (config.engine_profiler && g_gc_used && g_gc_heap)
    debugPrintf("[gc] managed used=%lld MB heap=%lld MB disabled=%d native_stw=%d stw_passes=%u\n",
                (long long)(g_gc_used() >> 20), (long long)(g_gc_heap() >> 20),
                g_gc_disabled ? g_gc_disabled() : -1, nx_gc_backend_ready(),
                nx_gc_stop_count());
  char name[96] = "?";
  if (g_icall_active_scene && g_icall_scene_name) {
    int handle = 0;
    g_icall_active_scene(&handle);
    const uint8_t *s = (const uint8_t *)g_icall_scene_name(handle);   /* Il2CppString */
    if (s) {
      const int32_t n = *(const int32_t *)(s + 16);
      const uint16_t *c = (const uint16_t *)(s + 20);
      int i = 0;
      for (; i < n && i < (int)sizeof name - 1; i++) name[i] = c[i] < 128 ? (char)c[i] : '?';
      name[i] = '\0';
    }
  }
  debugPrintf("[diag] кадр %d: time=%.2f unscaled=%.2f timeScale=%.2f dt=%.4f сцен=%d активная=%s\n",
              frame, g_icall_time ? g_icall_time() : -1.0f, g_icall_utime ? g_icall_utime() : -1.0f,
              g_get_timescale ? g_get_timescale() : -1.0f, nx_delta_time(),
              g_icall_scene_count ? g_icall_scene_count() : -1, name);
  nx_apply_physics_cap("повтор"); /* Independent of diagnostic verbosity. */
  if(!config.engine_profiler)return;
  if (g_icall_get_max_dt)
    debugPrintf("[physics] maximumDeltaTime=%.4f fixedDeltaTime=%.4f\n", g_icall_get_max_dt(),
                g_icall_get_fixed_dt ? g_icall_get_fixed_dt() : -1.0f);
  extern void nx_stat_cache_report(void);
  nx_stat_cache_report();
  /* Сам TimeManager: хук вызывался ли (g_tm), флаг паузы, точка отсчёта, шкала. */
  const uint8_t *tm = (const uint8_t *)nx_time_manager();
  if (tm)
    debugPrintf("[diag]   TimeManager %p (хук %s): пауза=%u/%u sref=%.3f time=%.3f unscaled=%.3f "
                "timeScale=%.2f fixedDt=%.4f; тиков потока часов %u\n",
                (const void *)tm, g_tm ? "да" : "нет", tm[0xf8], tm[0xc1],
                *(const double *)(tm + 0xe8), *(const double *)(tm + 0x90), *(const double *)(tm + 0xa0),
                *(const float *)(tm + 0xfc), g_get_fixed_delta ? g_get_fixed_delta() : -1.0f, g_clock_thread_ticks);
  else
    debugPrintf("[diag]   TimeManager не найден\n");
  /* Full thread snapshots are reserved for actual stalls/manual requests.
   * Periodically suspending healthy gameplay threads adds hitches and once
   * once exposed an unsafe raw stack scan. Lightweight counters stay enabled. */
}

uint32_t     port_frame_count(void){ return g_frame_count; } /* audio pump warmup gate */

/* Часы движка: перехват TimeManager::Update. Схема из vln_nx (NaGaa95, MIT) --
 * Unity-порта той же линии загрузчиков. Через 3600 кадров Time.time и
 * unscaledTime были 0, и ULTRAKILL навсегда стояла в сцене Bootstrap. Вход Update
 * перенаправляем сюда, повторяем её пролог (frameCount++, вспомогательный
 * счётчик, проверка паузы) и входим в тело функции (оно бескадровое, всё
 * перечитывает из x0) с newTime = точка отсчёта (+0xe8) + настенные часы.
 * Раскладка полей в 2022.3.29f1 та же: +0xc8, +0xd0, +0xf8, +0xe8. Адреса входа и
 * тела выводит tools/unity_syms.py по символу и сверке инструкций пролога.
 * Поток часов двигает время, пока главный поток стоит дольше 100 мс (синхронная
 * загрузка сцены) или если Update ещё ни разу не вызывалась. */
static void  (*g_unity_update_body)(void *, double) = NULL;
static void *(*g_unity_get_tm)(void) = NULL;               /* GetTimeManager() */
static void *volatile g_tm = NULL;
static Mutex g_clock_lock;
static uint64_t g_clk_base_ns = 0;
static volatile uint64_t g_last_main_tick_ns = 0;
static volatile uint32_t g_clock_thread_ticks = 0;
static uint32_t g_main_clock_seen = 0;
#define CLOCK_STALL_NS 100000000ULL

static void nx_clock_tick(void *tm) {
  const uint64_t now = nx_now_ns();
  if (!g_clk_base_ns) g_clk_base_ns = now;
  const double wall = (double)(now - g_clk_base_ns) / 1e9;
  const double sref = *(volatile double *)((char *)tm + 0xe8);
  g_unity_update_body(tm, sref + wall);
}
static void nx_time_update_hook(void *tm) {
  __atomic_store_n(&g_main_clock_seen, 1, __ATOMIC_RELEASE);
  g_tm = tm;
  g_last_main_tick_ns = nx_now_ns();
  *(volatile uint64_t *)((char *)tm + 0xc8) += 1;            /* frameCount++ (пролог) */
  *(volatile uint32_t *)((char *)tm + 0xd0) += 1;            /* вспомогательный счётчик */
  if (*(volatile uint8_t *)((char *)tm + 0xf8) != 0) return; /* пауза -> выход, как в движке */
  mutexLock(&g_clock_lock);
  nx_clock_tick(tm);
  mutexUnlock(&g_clock_lock);
}
static void *nx_time_manager(void) {
  void *tm = g_tm;
  if (!tm && g_unity_get_tm) tm = g_unity_get_tm();
  return tm;
}
static Thread g_clock_thr;
static void nx_clock_thread(void *arg) {
  (void)arg;
  static uint8_t clk_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  install_bionic_tls(clk_tls);
  for (;;) {
    svcSleepThread(8000000ULL);                               /* ~120 Гц */
    if (__atomic_load_n(&g_main_clock_seen, __ATOMIC_ACQUIRE)) return;
    void *tm = nx_time_manager();
    if (tm && !__atomic_load_n(&g_main_clock_seen, __ATOMIC_ACQUIRE) &&
        (nx_now_ns() - g_last_main_tick_ns) > CLOCK_STALL_NS &&
        *(volatile uint8_t *)((char *)tm + 0xf8) == 0 && mutexTryLock(&g_clock_lock)) {
      /* Never mutate game time from a worker once normal engine frames begin.
       * A slow frame is not permission to advance physics time concurrently. */
      if (!__atomic_load_n(&g_main_clock_seen, __ATOMIC_ACQUIRE)) nx_clock_tick(tm);
      mutexUnlock(&g_clock_lock);
      g_clock_thread_ticks++;
    }
  }
}
static void nx_install_time_fix(void) {
  const uint32_t entry = nxo_get("unity.tm_update");
  const uint32_t body  = nxo_get("unity.tm_update_body");
  const uint32_t gtm   = nxo_get("unity.get_time_manager");
  if (!entry || !body) {
    debugPrintf("[boot] в offsets.ini нет unity.tm_update(_body) -- часы движка не трогаю\n");
    return;
  }
  const uintptr_t ub = (uintptr_t)unity_mod.load_virtbase;
  g_unity_update_body = (void (*)(void *, double))(ub + body);
  if (gtm) g_unity_get_tm = (void *(*)(void))(ub + gtm);
  mutexInit(&g_clock_lock);
  uint32_t stub[4] = {
    0x58000050u,  /* ldr x16, #8 */
    0xd61f0200u,  /* br  x16     */
    (uint32_t)((uintptr_t)&nx_time_update_hook & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_time_update_hook >> 32),
  };
  so_patch_code((void *)(ub + entry), stub, sizeof stub);
  if (R_SUCCEEDED(threadCreate(&g_clock_thr, nx_clock_thread, NULL, NULL, 0x8000, 0x2C, -2)))
    threadStart(&g_clock_thr);
  debugPrintf("[boot] часы движка: TimeManager::Update @libunity+0x%x -> хук, тело +0x%x, поток часов запущен\n",
              entry, body);
}

/* libunity ~17M + libil2cpp ~36M + headroom for relocated segments */
#define SO_REGION_BYTES (160u * 1024 * 1024)

/* Reserve the virtual arena window at the TOP of the alias region (deep in the
 * 64GB region, where libnx never allocates) after verifying it is fully unmapped.
 * No physical backing yet -- pages are committed on demand. */
/* Unused on the current allocation path (the OC window is reserved in
 * libc_shim.c). Inherited from the base; kept for the alias-region experiments
 * documented around MMAP_VIRT_RESERVE in config.h. */
__attribute__((unused))
static void *overcommit_reserve_window(size_t size) {
  size = (size + MMAP_ARENA_ALIGN - 1) & ~(MMAP_ARENA_ALIGN - 1);
  if (!g_alias_base || g_alias_size < size + MMAP_ARENA_ALIGN) return NULL;
  u64 top = g_alias_base + g_alias_size;
  u64 win = (top - size) & ~(MMAP_ARENA_ALIGN - 1);
  u64 a = win;
  while (a < win + size) {
    MemoryInfo mi; u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) return NULL;
    if (mi.type != MemType_Unmapped) return NULL;   /* collision -> bail */
    a = mi.addr + mi.size;
  }
  return (void *)win;
}

/* Страницы TLS потоков ядро кладёт само, на случайный свободный адрес области
 * стека, и про резерв libnx (virtmemAddReservation) ничего не знает. Поэтому
 * страница нового потока Unity могла лечь внутрь окна OC, туда, где ещё нет
 * коммита, и следующий коммит на этом месте падал (svcMapMemory 0xd401,
 * InvalidCurrentMemory). Так закрывалась игра.
 * Лечение: до поиска окна создаём пачку потоков без запуска (каждый занимает
 * слот в странице TLS, по 8 слотов на страницу), затем закрываем все, кроме
 * каждого восьмого. Любые 8 подряд идущих потоков содержат один оставленный,
 * так что каждая полная страница остаётся с 7 свободными слотами. Новые потоки
 * ядро сначала сажает в такие страницы, и новых страниц не нужно. */
static void nx_tls_dummy_entry(void *arg) { (void)arg; svcExitThread(); }

static void nx_prealloc_tls(void) {
  enum { WANT = 96 };
  static u8 dummy_stack[0x1000] __attribute__((aligned(0x1000)));
  static Handle h[WANT];
  u64 lim = 0, cur = 0;
  u64 rl64 = 0;
  if (R_SUCCEEDED(svcGetInfo(&rl64, InfoType_ResourceLimit, INVALID_HANDLE, 0)) && rl64) {
    const Handle rl = (Handle)rl64;
    svcGetResourceLimitLimitValue((s64 *)&lim, rl, LimitableResource_Threads);
    svcGetResourceLimitCurrentValue((s64 *)&cur, rl, LimitableResource_Threads);
    svcCloseHandle(rl);
  }
  /* Временно занимаем не больше, чем свободно, с запасом в 16 потоков. */
  int want = WANT;
  if (lim && lim > cur) {
    const s64 room = (s64)(lim - cur) - 16;
    if (room < want) want = room > 0 ? (int)room : 0;
  }
  int made = 0;
  for (; made < want; made++)
    if (R_FAILED(svcCreateThread(&h[made], (void *)nx_tls_dummy_entry, NULL,
                                 dummy_stack + sizeof dummy_stack, 0x2C, -2)))
      break;
  int kept = 0;
  for (int i = 0; i < made; i++) {
    if (i % 8 == 0) kept++;
    else svcCloseHandle(h[i]);
  }
  debugPrintf("[tls] потоки: лимит %llu, занято %llu; заготовка %d -> держим %d (свободных слотов TLS ~%d)\n",
              (unsigned long long)lim, (unsigned long long)cur, made, kept, made - kept);
}

/* virtmemFindStack refuses large windows even when the stack region has room, so
 * scan the region directly via svcQueryMemory for the largest 256MB-aligned
 * unmapped hole (and log the whole map for diagnosis). svcMapMemory only aliases
 * into the stack region, so the OC window must live here. */
static void *oc_find_stack_window(size_t want, size_t *out_size) {
  *out_size = 0;
  u64 sbase = 0, ssize = 0;
  svcGetInfo(&sbase, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&ssize, InfoType_StackRegionSize,    CUR_PROCESS_HANDLE, 0);
  if (!sbase || !ssize) return NULL;
  u64 end = sbase + ssize, a = sbase, best_a = 0, best_l = 0;
  int holes = 0, mapped = 0;
  while (a < end) {
    MemoryInfo mi; u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) break;
    u64 ms = mi.addr, me = mi.addr + mi.size;
    if (me <= a) break;                              /* no-progress guard */
    if (mi.type == MemType_Unmapped) {
      u64 hs = ms < sbase ? sbase : ms, he = me > end ? end : me;
      if (he > hs) {
        if (he - hs > best_l) { best_l = he - hs; best_a = hs; }
        if (holes < 8)
          debugPrintf("[oc] stack hole %d: %p .. %p (%u MB)\n",
                      holes++, (void *)hs, (void *)he, (unsigned)((he - hs) >> 20));
      }
    } else mapped++;
    a = me;
  }
  debugPrintf("[oc] stack scan: base=%p size=%u MB, %d holes, %d mapped spans, largest=%u MB\n",
              (void *)sbase, (unsigned)(ssize >> 20), holes, mapped, (unsigned)(best_l >> 20));
  if (!best_a) return NULL;
  u64 aligned = (best_a + (MMAP_ARENA_ALIGN - 1)) & ~(MMAP_ARENA_ALIGN - 1);
  if (aligned >= best_a + best_l) return NULL;
  u64 avail = ((best_a + best_l) - aligned) & ~(MMAP_ARENA_ALIGN - 1);
  if (!avail) return NULL;
  if (avail > want) avail = want;
  *out_size = avail;
  return (void *)aligned;
}

/* The forwarder grants SystemResourceSize in its NPDM. Reserve address
 * space in Alias, shrink the physical heap, then test on-demand mapping. This
 * runs BEFORE malloc is initialized: only SVCs and static storage are allowed.
 * Preserve the heap prefix occupied by hbloader/NRO when using OverrideHeap.
 * A failed probe restores the old heap before the backed-arena fallback. */
static int overcommit_setup(void *addr, size_t size, size_t so_zone,
                            void **out_addr, size_t *out_fake) {
  g_oc_hint_map   = envIsSyscallHinted(0x2c);
  g_oc_hint_unmap = envIsSyscallHinted(0x2d);
  svcGetInfo(&g_alias_base, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&g_alias_size, InfoType_AliasRegionSize,    CUR_PROCESS_HANDLE, 0);
  g_oc_alias_mb = (unsigned)(g_alias_size >> 20);
  svcGetInfo(&g_oc_sysres, InfoType_SystemResourceSizeTotal, CUR_PROCESS_HANDLE, 0);
  if (!g_oc_hint_map || !g_oc_hint_unmap || !g_oc_sysres) return 0;
  void *win = overcommit_reserve_window(MMAP_VIRT_RESERVE);
  if (!win) return 0;

  u64 heap_base = 0;
  if (R_FAILED(svcGetInfo(&heap_base, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0)) ||
      !heap_base || (uintptr_t)addr < heap_base) return 0;
  const size_t alignment = 0x200000;
  const size_t prefix = (uintptr_t)addr - heap_base;
  const size_t old_size = prefix + size;
  const size_t newlib_size = (size_t)OVERCOMMIT_HEAP_MB * 1024 * 1024;
  const size_t keep = (prefix + newlib_size + so_zone + alignment - 1) & ~(alignment - 1);
  if (old_size % alignment || keep >= old_size) return 0;

  void *actual_base = NULL;
  g_oc_shrink_tried = 1;
  g_oc_shrink_rc = svcSetHeapSize(&actual_base, keep);
  if (R_FAILED(g_oc_shrink_rc)) return 0;
  if ((uintptr_t)actual_base != heap_base)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));

  g_oc_probe_tried = 1;
  g_oc_probe_rc = svcMapPhysicalMemory(win, 0x1000);
  if (R_FAILED(g_oc_probe_rc)) {
    Result restore = svcSetHeapSize(&actual_base, old_size);
    if (R_FAILED(restore)) diagAbortWithResult(restore);
    return 0;
  }
  Result unmap = svcUnmapPhysicalMemory(win, 0x1000);
  if (R_FAILED(unmap)) diagAbortWithResult(unmap);

  g_mmap_arena_base = win;
  g_mmap_arena_size = MMAP_VIRT_RESERVE;
  g_overcommit = 1;
  g_oc_heap_mb = (unsigned)(keep >> 20);
  g_oc_freed_mb = (unsigned)((old_size - keep) >> 20);
  g_oc_win = win;
  g_oc_win_addr = (unsigned long)win;
  *out_addr = addr;
  *out_fake = newlib_size;
  return 1;
}

/* Reserve a slice of address space for the .so images; the rest is the newlib
 * heap the engine mallocs from. (Verbatim from cr3_nx.) */
void __libnx_initheap(void) {
  void *addr;
  size_t size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  const size_t MB = 1024 * 1024;
  size_t so_zone = SO_REGION_BYTES;
  if (so_zone > size / 2)
    so_zone = size / 2;

  extern char *fake_heap_start;
  extern char *fake_heap_end;

  /* Preferred path: alias-region overcommit. Secures the virtual window and
   * test-commits a page FIRST, then shrinks the heap to [newlib + so_zone] so the
   * freed physical (~2.5GB) is available for on-demand commits. Everything is
   * secured before the shrink so a failure can't strand us with a shrunk heap. */
  void *oc_addr; size_t oc_fake;
  if (overcommit_setup(addr, size, so_zone, &oc_addr, &oc_fake)) {
    fake_heap_start = (char *)oc_addr;
    fake_heap_end   = (char *)oc_addr + oc_fake;
    heap_so_base    = (void *)ALIGN_MEM((uintptr_t)oc_addr + oc_fake, 0x1000);
    heap_so_limit   = so_zone;
    return;
  }

  /* Fallback: fully heap-backed 256MB-aligned arena (no overcommit). */
  const size_t big_align    = MMAP_ARENA_ALIGN;
  const size_t newlib_floor = 448 * MB;   /* malloc + il2cpp managed/GC heap */
  size_t arena_sz = MMAP_ARENA_RESERVE;
  size_t fake_heap_size;

  if (size > so_zone + big_align + newlib_floor + 256 * MB) {
    size_t avail = size - so_zone - big_align - newlib_floor;
    if (arena_sz > avail) arena_sz = avail & ~(big_align - 1);   /* clamp to RAM */
    fake_heap_size = size - so_zone - arena_sz - big_align;       /* newlib gets the rest */
  } else {
    /* heap too small for a dedicated arena (e.g. applet mode): skip it; the mmap
     * allocator falls back to a memalign-backed bitmap arena. */
    fake_heap_size = (size > so_zone) ? size - so_zone : size / 2;
    arena_sz = 0;
  }

  fake_heap_start = (char *)addr;
  fake_heap_end   = (char *)addr + fake_heap_size;

  heap_so_base  = (void *)ALIGN_MEM((uintptr_t)addr + fake_heap_size, 0x1000);
  heap_so_limit = so_zone;

  if (arena_sz) {
    g_mmap_arena_base = (void *)ALIGN_MEM((uintptr_t)heap_so_base + so_zone, big_align);
    g_mmap_arena_size = arena_sz;
  }
}

static void check_syscalls(void) {
  if (!envIsSyscallHinted(0x77)) fatal_error("svcMapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x78)) fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x73)) fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if (envGetOwnProcessHandle() == INVALID_HANDLE) fatal_error("Own process handle is unavailable.");
}

/* Verify the SD layout before we touch the engine.
 *
 * Unity games ship either the single-bundle layout (data.unity3d) or the
 * classic non-bundled one (globalgamemanagers + level0 + sharedassets0 +
 * friends); demanding one specific file makes the loader refuse a valid game.
 *
 * So this checks for what is actually there, and accepts EITHER layout:
 * a build with data.unity3d is fine, and so is one without. The only things
 * treated as fatal are the ones nothing can proceed without.
 *
 * Everything else is reported but not fatal. A missing optional file gives a
 * named warning in debug.log, which is far more useful than a hard stop -- the
 * player finds out which file to re-copy instead of just "check your SD card".
 */
static int have_rel(const char *rel) {
  char path[768];
  struct stat st;
  game_path(path, sizeof path, rel);
  return stat(path, &st) == 0;
}

static void check_data(void) {
  /* --- 1. the engine libraries: nothing works without these --------------- */
  const char *libs[] = { LIB_MAIN, LIB_UNITY, LIB_IL2CPP };
  for (unsigned i = 0; i < sizeof(libs)/sizeof(*libs); i++) {
    if (!have_rel(libs[i]))
      fatal_error("Missing engine library:\n%s\n\n"
                  "Copy it from your APK's lib/arm64-v8a/ folder into\n%s",
                  libs[i], DATA_ROOT);
  }

  /* --- 2. il2cpp metadata: the managed side cannot start without it ------- */
  if (!have_rel("assets/bin/Data/Managed/Metadata/global-metadata.dat"))
    fatal_error("Missing assets/bin/Data/Managed/Metadata/global-metadata.dat\n\n"
                "Copy the whole assets/ folder from your APK into\n%s", DATA_ROOT);

  /* --- 3. the scene data, in EITHER layout -------------------------------- */
  int bundled  = have_rel("assets/bin/Data/data.unity3d");
  int classic  = have_rel("assets/bin/Data/globalgamemanagers");
  if (!bundled && !classic)
    fatal_error("No Unity scene data found under\n%s/assets/bin/Data\n\n"
                "Expected either globalgamemanagers (this game's layout)\n"
                "or data.unity3d. Copy the whole assets/ folder from your APK.",
                DATA_ROOT);
  debugPrintf("[boot] asset layout: %s\n",
              bundled ? "bundled (data.unity3d)" : "classic (globalgamemanagers)");

  /* --- 4. split-file targets: joined, or still in pieces ------------------ */
  if (classic) {
    const char *split_targets[] = {
      "assets/bin/Data/globalgamemanagers.assets",
      "assets/bin/Data/sharedassets0.assets",
    };
    for (unsigned i = 0; i < sizeof(split_targets)/sizeof(*split_targets); i++) {
      char s0[768];
      snprintf(s0, sizeof s0, "%s.split0", split_targets[i]);
      if (!have_rel(split_targets[i]) && !have_rel(s0))
        fatal_error("Missing %s\n(and no .split0 to rebuild it from)\n\n"
                    "Copy the whole assets/ folder from your APK.",
                    split_targets[i]);
    }
  }

  /* --- 5. everything else: warn, never stop ------------------------------ */
  const char *optional[] = {
    "assets/bin/Data/level0",
    "assets/bin/Data/boot.config",
    "assets/bin/Data/unity_app_guid",
    "assets/bin/Data/sharedassets0.resource",
    "assets/bin/Data/unity default resources",
    "assets/bin/Data/Resources/unity_builtin_extra",
    "assets/bin/Data/Managed/Resources/mscorlib.dll-resources.dat",
    "assets/loc/en.zip",
  };
  int missing = 0;
  for (unsigned i = 0; i < sizeof(optional)/sizeof(*optional); i++) {
    if (!have_rel(optional[i])) {
      debugPrintf("[boot] NOTE: %s is missing (continuing)\n", optional[i]);
      missing++;
    }
  }
  debugPrintf("[boot] asset check passed%s\n",
              missing ? " (with notes above)" : "");
}

/* load a module, advance the .so arena, resolve its imports against the table */
static int load_module(so_module *mod, const char *name) {
  char path[768];
  game_path(path, sizeof path, name);
  if (so_load(mod, path, heap_so_base, heap_so_limit) < 0)
    return -1;
  size_t used = ALIGN_MEM(mod->load_size, 0x1000);
  heap_so_base = (char *)heap_so_base + used;
  heap_so_limit -= used;
  debugPrintf("[mod] %-14s virtbase=%p size=0x%zx  (resolve: addr - virtbase = vaddr)\n",
              name, mod->load_virtbase, mod->load_size);
  crx_resolve_imports(mod);   /* so_resolve(mod, dynlib_functions, ...) */
  /* NOTE: so_patch_stack_canaries() intentionally NOT called. Per-thread bionic
   * TLS (install_bionic_tls) makes the engine's stack-protector guard consistent,
   * so the canary checks pass on their own. NOPing 2000+ b.ne sites risked a
   * false-positive in non-canary code (e.g. allocator list logic) -> corruption. */
  return 0;
}

/* Точка входа UnityPlayer по имени: сначала из RegisterNatives, затем из
 * offsets.ini (unity.<имя> = смещение в libunity). */
extern void *jni_native(const char *name);
static void *unity_native(so_module *mod, const char *name) {
  void *fn = jni_native(name);
  if (fn) return fn;
  char key[96];
  snprintf(key, sizeof key, "unity.%s", name);
  uint32_t off = nxo_get(key);
  fn = off ? (void *)((uintptr_t)mod->load_virtbase + off) : NULL;
  debugPrintf("[boot] native %s not registered; offsets.ini -> %p\n", name, fn);
  return fn;
}

/* engine entry points (unity_entrypoints.h), resolved post-finalize */
static fn_initJni  Unity_initJni;
static fn_gfxstate Unity_nativeRecreateGfxState;
static fn_v        Unity_nativeSendSurfaceChanged;
static fn_z        Unity_nativeRender;
static fn_inject   Unity_nativeInjectEvent;
static fn_v        Unity_nativeResume;
static fn_vz       Unity_nativeFocusChanged;
static fn_z        Unity_nativeDone;
static fn_v        Unity_nativeApplicationUnload;

/* ---------------------------------------------------------------------------
 * Заплатки libunity из offsets.ini. Адреса у каждой сборки движка свои, поэтому
 * их выводит tools/unity_syms.py по символам libunity.sym.so и шаблонам
 * инструкций, а здесь только проверка и запись:
 *   patch.unity.<имя> = адрес было стало   -- слово кода; пишется, только если
 *                                             на месте ровно «было» (иначе это
 *                                             другая сборка -- пропуск с записью
 *                                             в журнал)
 *   poke8.unity.<имя> = адрес значение     -- байт в данных (флаги вроде
 *                                             Swappy::s_ForceDisable)
 * ------------------------------------------------------------------------- */
static void nx_apply_patch(void *ctx, const char *key, const uint32_t *v) {
  const uintptr_t ub = *(const uintptr_t *)ctx;
  volatile uint32_t *at = (volatile uint32_t *)(ub + v[0]);
  const uint32_t cur = *at;
  if (cur == v[2]) {
    debugPrintf("[patch] %s @+0x%x уже стоит\n", key, v[0]);
  } else if (cur != v[1]) {
    debugPrintf("[patch] %s @+0x%x: 0x%08x, ждали 0x%08x -- ПРОПУСК (другая сборка libunity)\n",
                key, v[0], cur, v[1]);
  } else {
    so_patch_code((void *)at, &v[2], sizeof v[2]);
    debugPrintf("[patch] %s @+0x%x: 0x%08x -> 0x%08x\n", key, v[0], v[1], *at);
  }
}

static void nx_apply_poke8(void *ctx, const char *key, const uint32_t *v) {
  const uintptr_t ub = *(const uintptr_t *)ctx;
  *(volatile uint8_t *)(ub + v[0]) = (uint8_t)v[1];
  debugPrintf("[poke] %s @+0x%x = %u\n", key, v[0], v[1]);
}

static void nx_patch_libunity(uintptr_t ub) {
  int np = nxo_each("patch.unity.", 3, nx_apply_patch, &ub);
  int nk = nxo_each("poke8.unity.", 2, nx_apply_poke8, &ub);
  if (!np && !nk)
    debugPrintf("[patch] в offsets.ini нет patch.unity.* -- движок без заплаток "
                "(время, звук и Swappy работать не будут)\n");
}

/* Путь приложения движка -- буфер, который заполняет FileSystem::SetApplicationPath
 * (unity.app_path_buf в offsets.ini). Unity пишет его, только если путь из
 * getPackageCodePath открылся как zip. У нас APK нет, и без этой записи пустыми
 * остаются Application.dataPath (игра падает в Directory.GetParent("")),
 * папка данных и streamingAssetsPath (Addressables не находит каталог). Пишем
 * после init_array: статические конструкторы могли бы его обнулить. */
static void nx_set_app_path(uintptr_t ub) {
  const uint32_t off = nxo_get("unity.app_path_buf");
  if (!off) {
    debugPrintf("[boot] в offsets.ini нет unity.app_path_buf -- dataPath останется пустым\n");
    return;
  }
  snprintf((char *)(ub + off), 0x410, "%s", game_apk_path());
  debugPrintf("[boot] путь приложения @+0x%x = %s\n", off, (const char *)(ub + off));
}

/* Shader.maximumChunksOverride, before the first Shader::Transfer. Zero (the
 * game's default) eagerly decompresses every chunk of Master (~1.9 GB).
 * The configured chunk count bounds its code cache, with Unity's own eviction and
 * locking; all variants remain available. Call after static constructors, which
 * initialize the override to -1, and before any engine lifecycle entry point. */
static void nx_limit_shader_cache(uintptr_t ub) {
  const uint32_t set_off = nxo_get("unity.shader_set_max_chunks");
  const uint32_t get_off = nxo_get("unity.shader_get_max_chunks");
  if (!set_off || !get_off)
    fatal_error("offsets.ini lacks Shader chunk-cache accessors.\nCopy the complete updated port folder.");
  void (*set_limit)(int) = (void (*)(int))(ub + set_off);
  int (*get_limit)(void) = (int (*)(void))(ub + get_off);
  const int before = get_limit();
  const int limit = config.shader_chunks;
  set_limit(limit);
  const int after = get_limit();
  debugPrintf("[shader-cache] maximumChunksOverride %d -> %d (16 MiB chunks per shader)\n",
              before, after);
  if (after != limit)
    fatal_error("Shader chunk-cache override failed: %d", after);
}

extern uint32_t g_port_exceptions;
static uint32_t g_trace_exception_base;
static int g_trace_exception_active;

/* Default 60 FPS; explicit PC rates and None are preserved. Read FPSLocker into this same
 * limiter instead of sleeping independently on the render thread. */
static volatile int g_fps_target;
static NxFpsPolicy g_fps_policy;
int nx_fps_target(void) { return g_fps_target; }
static int nx_target_fps(void) {
  extern int nx_preferences_frame_rate(void);
  const int fps = nx_fps_resolve(nx_preferences_frame_rate(), config.fps_limit, nx_salty_fps_limit());
  g_fps_target = fps;
  return fps;
}
static void nx_fps_enforce(void) {
  const uintptr_t ub = (uintptr_t)unity_mod.load_virtbase;
  static uint32_t get, set;
  if (!get) get = nxo_get("unity.app_get_target_fps");
  if (!set) set = nxo_get("unity.app_target_fps");
  if (!get || !set) return;
  const int now = ((int (*)(void))(ub + get))();
  const int want = nx_fps_observe(&g_fps_policy, now, config.fps_limit, nx_salty_fps_limit());
  g_fps_target = want;
  if (now == want) return;
  ((void (*)(int))(ub + set))(want);
  static unsigned reported;
  if (reported++ < 8) debugPrintf("[fps] targetFrameRate %d -> %d (fps_limit %d)\n", now, want, config.fps_limit);
}

/* Called immediately after the game's menu setter, on UnityMain. */
void nxb_fps_request(int requested) {
  const int want=nx_fps_select(&g_fps_policy,requested,config.fps_limit,nx_salty_fps_limit());
  g_fps_target=want;
  const uint32_t set=nxo_get("unity.app_target_fps");
  if(set)((void (*)(int))((uintptr_t)unity_mod.load_virtbase+set))(want);
  debugPrintf("[fps] menu request=%d effective=%d overlay=%d\n",requested,want,nx_salty_fps_limit());
}

static void nx_apply_performance_profile(void) {
  const uintptr_t ub = (uintptr_t)unity_mod.load_virtbase;
  const uint32_t trace = nxo_get("unity.app_stacktrace");
  if (trace) {
    g_trace_exception_base = __atomic_load_n(&g_port_exceptions, __ATOMIC_RELAXED);
    g_trace_exception_active = 1;
    ((void (*)(int, int))(ub + trace))(4, 2); /* Exception, Full */
  }
  if (!config.performance_mode && !config.fsr) return;
  const uint32_t resolution = nxo_get("unity.screen_resolution");
  if (resolution) {
    const struct { uint32_t numerator, denominator; } refresh = { 60, 1 };
    /* Keep the real NWindow unchanged. Unity's existing software-resolution
     * fallback renders into this smaller target and blits to the native panel. */
    /* The display stays native even with the filter disabled. NxFsr owns the
     * low-resolution composition and can switch bilinear/FSR at runtime. */
    const int output_height = 720;
    ((void (*)(int, int, int, const void *))(ub + resolution))(
        output_height * 16 / 9, output_height, 1, &refresh);
    debugPrintf("[perf-profile] Unity output %dx%d world_height=%d fsr_requested=%d\n",
                output_height * 16 / 9, output_height, config.render_height, config.fsr);
  }
  if (!config.performance_mode) return;
  /* The physics timestep is never changed: the game's scripts assume 125 Hz,
   * and a 60 Hz override made the whole game run at a different speed. */
  const struct { const char *key; int value; } settings[] = {
    { "unity.quality_mip_limit", 1 }, /* half width/height for mipmapped textures */
    { "unity.quality_shadows", 0 },
    { "unity.quality_aa", 0 },
    { "unity.app_target_fps", nx_target_fps() },
  };
  for (unsigned i = 0; i < sizeof settings / sizeof settings[0]; ++i) {
    const uint32_t off = nxo_get(settings[i].key);
    if (off) {
      ((void (*)(int))(ub + off))(settings[i].value);
      debugPrintf("[perf-profile] %s = %d\n", settings[i].key, settings[i].value);
    } else debugPrintf("[perf-profile] missing %s\n", settings[i].key);
  }
  /* В проекте m_ReuseCollisionCallbacks 0 -- на каждый OnCollision* при 125 Гц
   * физики создаётся новый Collision, и это мусор для GC (паузы до 215 мс).
   * Скрипты игры объект Collision не хранят: в Assembly-CSharp нет ни полей,
   * ни замыканий, ни корутин с этим типом. */
  const uint32_t reuse_get = nxo_get("unity.physics_get_reuse_callbacks");
  const uint32_t reuse_set = nxo_get("unity.physics_reuse_callbacks");
  if (!config.collision_reuse) debugPrintf("[perf-profile] Physics.reuseCollisionCallbacks: выключено в config.txt\n");
  else if (reuse_get && reuse_set) {
    const int was = ((unsigned char (*)(void))(ub + reuse_get))();
    ((void (*)(unsigned char))(ub + reuse_set))(1);
    debugPrintf("[perf-profile] Physics.reuseCollisionCallbacks %d -> %d\n",
                was, ((unsigned char (*)(void))(ub + reuse_get))());
  } else debugPrintf("[perf-profile] missing unity.physics_reuse_callbacks\n");
  /* В бою шаг физики около 2 мс, почти половину главный поток ждёт потоки PhysX.
   * Итерации решателя берутся телом при создании, поэтому ставятся до уровней. */
  const uint32_t solver_get = nxo_get("unity.physics_get_solver_iterations");
  const uint32_t solver_set = nxo_get("unity.physics_solver_iterations");
  if (!config.physics_solver) debugPrintf("[perf-profile] Physics.defaultSolverIterations: как в игре (config.txt)\n");
  else if (solver_get && solver_set) {
    const int was = ((int (*)(void))(ub + solver_get))();
    ((void (*)(int))(ub + solver_set))(config.physics_solver);
    debugPrintf("[perf-profile] Physics.defaultSolverIterations %d -> %d\n",
                was, ((int (*)(void))(ub + solver_get))());
  } else debugPrintf("[perf-profile] missing unity.physics_solver_iterations\n");
  /* libil2cpp собирается с --incremental-g-c-time-slice=3 (docs/BUILD.md) */
  const uint32_t gc_inc = nxo_get("unity.gc_is_incremental");
  if (gc_inc) debugPrintf("[perf-profile] GarbageCollector.isIncremental = %d\n",
                          ((unsigned char (*)(void))(ub + gc_inc))());
}

/* Unity копирует Managed в il2cpp/ один раз и отмечает это в il2cpp/unity.ver.
 * Если копирование сорвалось, отметка остаётся, а метаданных нет -- и каждый
 * следующий запуск закрывается на «Could not open global-metadata.dat».
 * Без метаданных снимаем отметку, чтобы Unity скопировала заново. */
static void nx_check_il2cpp_cache(void) {
  char ver[320], meta[320];
  struct stat st;
  game_path(ver, sizeof ver, "il2cpp/unity.ver");
  game_path(meta, sizeof meta, "il2cpp/Metadata/global-metadata.dat");
  if (stat(meta, &st) != 0 || st.st_size == 0) {
    if (unlink(ver) == 0)
      debugPrintf("[boot] нет il2cpp/Metadata -- снял unity.ver, Unity скопирует заново\n");
  }
}

int main(int argc, char *argv[]) {
  if (g_overcommit) {
    virtmemLock();
    VirtmemReservation *rv = virtmemAddReservation(g_mmap_arena_base, g_mmap_arena_size);
    virtmemUnlock();
    if (!rv) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }
  /* FIRST: work out which folder under sdmc:/switch we are running from, before
   * anything tries to open the log (which lives in it). Resolution order is
   * argv[0] -> scan sdmc:/switch for the folder holding libil2cpp.so ->
   * the compile-time GAME_HOME default. See nx_gameroot.c. */
  game_root_init(argc, argv);

  socketInitializeDefault();
  debugPrintf("[boot] === ultrakill_nx r93 (Unity/GLES / lightweight runtime diagnostics; render detours opt-in) ===\n");
  nx_cpu_init();
  {
    u64 program = 0;
    svcGetInfo(&program, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    debugPrintf("[boot] launch process=%016llx system_resource=%u MB demand_memory=%d "
                "shrink_rc=0x%x probe_rc=0x%x\n", (unsigned long long)program,
                (unsigned)(g_oc_sysres >> 20), g_overcommit, g_oc_shrink_rc, g_oc_probe_rc);
  }
  if (!g_overcommit) {
    debugPrintf("[boot] demand-memory setup unavailable; the current forwarder is required\n");
    fatal_error("ULTRAKILL NX: launcher memory setup failed.\n\n"
                "Install the NEW ULTRAKILL_NX_forwarder.nsp from the SD card\n"
                "using DBI, then launch ULTRAKILL NX from HOME.\n\n"
                "System resource: %u MB, alias: %u MB\n"
                "Heap resize: 0x%x, memory probe: 0x%x\n\n"
                "Details: switch/ultrakill/debug.log",
                (unsigned)(g_oc_sysres >> 20), g_oc_alias_mb, g_oc_shrink_rc, g_oc_probe_rc);
  }
  nxo_load();
  debugPrintf("[boot] game root resolved to '%s' (argv0=%s)\n",
              DATA_ROOT, (argc > 0 && argv && argv[0]) ? argv[0] : "(none)");

  /* Load config.txt. Missing -> autogenerate a documented one with the defaults;
   * holding retired options (`language`, and the old landscape `portrait 0`)
   * -> rewrite it without them. Language is no longer a setting here: the game
   * has its own in-game selector, and lang_code() just reports the Switch system
   * language as the initial choice. */
  {
    extern const char *nx_resolved_language(void);   /* jni_fake.c */
    char cfgpath[320];
    game_path(cfgpath, sizeof cfgpath, CONFIG_NAME);
    struct stat cfgstat;
    if (stat(cfgpath,&cfgstat)!=0) {
      char backup[336];snprintf(backup,sizeof backup,"%s.bak",cfgpath);
      rename(backup,cfgpath);
    }
    int crc = read_config(cfgpath);
    if (crc != 0)
      write_config(cfgpath);
    debugPrintf("[boot] config: docked_1080p=%d gyro=%d gyro_sens=%.2f, dpi=%d, initial language=%s%s\n",
                config.docked_1080p, config.gyro, (double)config.gyro_sens,
                nx_screen_dpi(),
                nx_resolved_language(),
                crc < 0 ? "  [config.txt created]" : crc > 0 ? "  [config.txt rewritten]" : "");
  }
  nx_cpu_apply_config();
  /* env кэша шейдеров Mesa -- до eglInitialize в libunity (imports.c) */
  extern void nx_shader_cache_setup(int mode);
  nx_shader_cache_setup(config.gl_program_cache);

  extern void nx_support_prepare(void);
  nx_support_prepare();
  if (config.saltynx) nx_salty_init();
  nx_loading_init();
  extern void nx_preferences_prepare(void);
  nx_preferences_prepare();

  /* Sweep Unity's case-sensitivity probe files: CASESENSITIVETEST<guid> strays
   * from older builds, plus the single hidden scratch the probe is redirected
   * to now (libc_shim.c casetest_redirect). */
  {
    DIR *dd = opendir(DATA_ROOT);
    int swept = 0;
    if (dd) {
      struct dirent *de;
      while ((de = readdir(dd))) {
        if (strncasecmp(de->d_name, "CASESENSITIVETEST", 17) == 0 ||
            strcmp(de->d_name, ".casetest") == 0) {
          char pth[320]; snprintf(pth, sizeof pth, "%s/%s", DATA_ROOT, de->d_name);
          if (unlink(pth) == 0) swept++;
        }
      }
      closedir(dd);
    }
    if (swept) debugPrintf("[boot] swept %d case-sensitivity probe file(s)\n", swept);
  }

  /* The game uses the classic non-bundled Unity layout, and an Android build may
   * split the big serialized files into 1 MB chunks (.split0 .. .splitN). libunity has a
   * native reader for those, but it is bound to the APK/AAssetManager path we
   * do not have -- our files are loose on the SD card. Put them back together
   * once, here, into the names the engine expects in the ordinary case.
   * Idempotent, verified against each file's own header, and a no-op after the
   * first launch. Must happen BEFORE the engine libraries load. */
  nx_assets_prepare();

  /* CWD fix (mirrors MMX enter_data_dir): title-override / hbloader leaves the
   * working dir at the .nro folder or the SD root, NOT the game dir. Unity &
   * il2cpp read many files through *relative* paths ("assets/bin/Data/...") and
   * our basename_fallback stats relative to cwd, so a wrong cwd silently yields
   * empty/missing reads -> NULL il2cpp classes. chdir into DATA_ROOT so every
   * relative read resolves under the game folder. (Absolute "sdmc:/..."
   * reads are unaffected.) */
  {
    char cwd[256] = {0};
    getcwd(cwd, sizeof cwd);
    int rc = chdir(DATA_ROOT);
    char cwd2[256] = {0};
    getcwd(cwd2, sizeof cwd2);
    struct stat st;
    /* Probe files this game actually has. */
    int reach_ggm  = stat("assets/bin/Data/globalgamemanagers", &st) == 0;
    int reach_meta = stat("assets/bin/Data/Managed/Metadata/global-metadata.dat", &st) == 0;
    int reach_guid = stat("assets/bin/Data/unity_app_guid", &st) == 0;
    int reach_lvl0 = stat("assets/bin/Data/level0", &st) == 0;
    debugPrintf("[boot] cwd was '%s' -> chdir(%s)=%d -> '%s'\n", cwd, DATA_ROOT, rc, cwd2);
    debugPrintf("[boot] reachable(rel): globalgamemanagers=%d level0=%d metadata=%d unity_app_guid=%d\n",
                reach_ggm, reach_lvl0, reach_meta, reach_guid);
  }

  /* Force libunity to RE-EXTRACT il2cpp resources every boot. Observed: when
   * extraction is skipped (il2cpp/unity.ver present), il2cpp mmaps the extracted
   * global-metadata.dat and crashes in Class::Init(NULL); when extraction RUNS,
   * il2cpp uses the full source it reads for the copy and gets past that point.
   * The extracted copy is bad because our shim doesn't flush a writable
   * file-backed mmap back to disk, so it lands truncated. Removing the extracted
   * markers makes libunity redo the extraction each boot (uses the good source).
   * Proper fix = flush writable file-backed mmaps on munmap (tracked separately). */
  {
    char p1[320], p2[320], p3[320];
    game_path(p1, sizeof p1, "il2cpp/unity.ver");
    game_path(p2, sizeof p2, "il2cpp/Metadata/global-metadata.dat");
    game_path(p3, sizeof p3, "il2cpp/Resources/mscorlib.dll-resources.dat");
    int a = unlink(p1);
    int b = unlink(p2);
    int c = unlink(p3);
    debugPrintf("[boot] force re-extract: unlink unity.ver=%d metadata=%d resources=%d\n", a, b, c);
  }

  check_syscalls();
  debugPrintf("[boot] syscalls ok\n");
  {
    extern char *fake_heap_start, *fake_heap_end;
    debugPrintf("[boot] mem layout: newlib=%u MB, mmap arena=%u MB @ %p\n",
                (unsigned)((fake_heap_end - fake_heap_start) / (1024 * 1024)),
                (unsigned)(g_mmap_arena_size / (1024 * 1024)), g_mmap_arena_base);
    if (g_overcommit)
      debugPrintf("[boot] OVERCOMMIT on: heap shrunk to %u MB, freed %u MB physical; "
                  "arena reserved virtual @ %p (commit on demand)\n",
                  g_oc_heap_mb, g_oc_freed_mb, g_mmap_arena_base);
    else
      debugPrintf("[boot] OVERCOMMIT off (heap-backed): system_resource=%u MB "
                  "(svcMapPhysicalMemory needs >0; unsafe pool exhausted). map_hint=%d alias=%u MB\n",
                  (unsigned)(g_oc_sysres >> 20), g_oc_hint_map, g_oc_alias_mb);
  }

  /* Overcommit feasibility probe. Proper PROT_NONE overcommit on Switch needs a
   * physical-backing primitive (svcMapMemory in the stack region, or
   * svcMapPhysicalMemory in the alias region) plus a region large enough to hold
   * Unity's multi-GB reservations. MMX found svcMapMemory caps at ~2-3 pools (the
   * stack region is ~1GB). Log the region sizes + which mapping svc are granted so
   * we can size/choose the real overcommit (or rule it out) from real numbers. */
  {
    struct { const char *nm; int a, s; } R[] = {
      { "alias", InfoType_AliasRegionAddress, InfoType_AliasRegionSize },
      { "heap",  InfoType_HeapRegionAddress,  InfoType_HeapRegionSize  },
      { "stack", InfoType_StackRegionAddress, InfoType_StackRegionSize },
    };
    for (unsigned i = 0; i < 3; i++) {
      u64 a = 0, s = 0;
      svcGetInfo(&a, R[i].a, CUR_PROCESS_HANDLE, 0);
      svcGetInfo(&s, R[i].s, CUR_PROCESS_HANDLE, 0);
      debugPrintf("[probe] region %-5s base=0x%lx size=%u MB\n",
                  R[i].nm, (unsigned long)a, (unsigned)(s >> 20));
    }
    u64 tot = 0, used = 0;
    svcGetInfo(&tot,  InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
    debugPrintf("[probe] mem total=%u MB used=%u MB free=%u MB\n",
                (unsigned)(tot >> 20), (unsigned)(used >> 20),
                (unsigned)((tot - used) >> 20));
    debugPrintf("[probe] svc hinted: MapPhysicalMemory(0x2c)=%d UnmapPhysical(0x2d)=%d "
                "MapMemory(0x24)=%d UnmapMemory(0x25)=%d\n",
                envIsSyscallHinted(0x2c), envIsSyscallHinted(0x2d),
                envIsSyscallHinted(0x24), envIsSyscallHinted(0x25));
  }

  /* Decisive probe: does svcMapMemory accept a dst in the (unmapped upper) HEAP
   * region? The stack region works but is only ~2GB (~8 regions). The heap region
   * is 8GB; its upper ~5GB sits unmapped above our heap. If svcMapMemory works
   * there too, we can host Unity's 256MB pools in ~7GB of backable address space
   * (~28 regions) without any libunity patching. Pure diagnostic: map 1 page,
   * verify the sentinel reads back, unmap. */
  {
    u64 hbase = 0, hsize = 0;
    svcGetInfo(&hbase, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&hsize, InfoType_HeapRegionSize,    CUR_PROCESS_HANDLE, 0);
    u64 probe = 0, a = hbase, end = hbase + hsize;
    while (a < end) {
      MemoryInfo mi; u32 pi;
      if (R_FAILED(svcQueryMemory(&mi, &pi, a))) break;
      if (mi.addr + mi.size <= a) break;
      if (mi.type == MemType_Unmapped && mi.size >= MMAP_ARENA_ALIGN) {
        u64 al = (mi.addr + (MMAP_ARENA_ALIGN - 1)) & ~(MMAP_ARENA_ALIGN - 1);
        if (al + 0x1000 <= mi.addr + mi.size) { probe = al; break; }
      }
      a = mi.addr + mi.size;
    }
    if (probe) {
      void *src = memalign(0x1000, 0x1000);
      if (src) {
        *(volatile u32 *)src = 0xABCD1234;
        Result rc = svcMapMemory((void *)probe, src, 0x1000);
        if (R_SUCCEEDED(rc)) {
          u32 v = *(volatile u32 *)probe;
          Result u = svcUnmapMemory((void *)probe, src, 0x1000);
          debugPrintf("[heapprobe] heap-region svcMapMemory @ 0x%lx rc=0x%x read=0x%x unmap=0x%x WORKS=%d\n",
                      (unsigned long)probe, rc, v, u, v == 0xABCD1234);
          if (R_SUCCEEDED(u)) free(src);
        } else {
          debugPrintf("[heapprobe] heap-region svcMapMemory @ 0x%lx FAILED rc=0x%x\n",
                      (unsigned long)probe, rc);
          free(src);
        }
      }
    } else {
      debugPrintf("[heapprobe] no unmapped 256MB-aligned spot found in heap region\n");
    }
  }

  /* Arm the stack-region overcommit arena. The boot probe confirmed svcMapMemory
   * aliases heap pages into the stack region; Unity reserves ~2.8GB of PROT_NONE
   * pools but commits only ~80MB. Reserve a 1280MB stack-region window (cheap
   * address space) + a 256MB heap commit-pool; the OC arena (libc_shim.c) then
   * holds Unity's big reservations there and aliases pool pages in on mprotect.
   * Any failure leaves OC disabled and the engine runs on the heap-backed arena. */
  if (!g_overcommit) {
    void *pool = NULL;
    size_t winsz = 0;
    nx_prealloc_tls();   /* до поиска окна: страницы TLS не должны лечь в него */
    void *win = oc_find_stack_window(OC_WINDOW_BYTES, &winsz);
    VirtmemReservation *rv = NULL;
    if (win && winsz) {
      virtmemLock();
      rv = virtmemAddReservation(win, winsz);   // keep libnx thread stacks out
      virtmemUnlock();
    }
    if (win && rv && winsz) {
      pool = memalign(0x1000, OC_POOL_BYTES);
      if (pool && oc_arena_init(win, winsz, pool, OC_POOL_BYTES))
        debugPrintf("[oc] ARMED: window %u MB @ %p, pool %u MB @ %p, heap-backed arena %u MB "
                    "(total reserve %u MB)\n",
                    (unsigned)(winsz >> 20), win, (unsigned)(OC_POOL_BYTES >> 20), pool,
                    (unsigned)(g_mmap_arena_size >> 20),
                    (unsigned)((winsz + g_mmap_arena_size) >> 20));
      else
        debugPrintf("[oc] DISABLED: pool=%p init failed -> heap-backed only\n", pool);
    } else {
      debugPrintf("[oc] DISABLED: no usable stack hole (win=%p sz=%u MB rv=%p) -> heap-backed only\n",
                  win, (unsigned)(winsz >> 20), (void *)rv);
    }
  }

  /* Report the surface size everywhere Unity reads dimensions (config.h,
   * screen_width/screen_height). */
  android_native_update_mode();   /* альбомная поверхность: 1280x720, в доке по config */

  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    debugPrintf("SDL_Init failed: %s\n", SDL_GetError());

  check_data();

  /* load the three modules; libil2cpp resolves its engine calls against libunity
   * module-to-module during relocation. */
  debugPrintf("[boot] loading modules...\n");
  if (load_module(&main_mod,   LIB_MAIN)   < 0) fatal_error("Could not load %s", LIB_MAIN);
  debugPrintf("[boot] loaded libmain   @ virtbase %p\n", (void *)main_mod.load_virtbase);
  if (load_module(&unity_mod,  LIB_UNITY)  < 0) fatal_error("Could not load %s", LIB_UNITY);
  debugPrintf("[boot] loaded libunity  @ virtbase %p\n", (void *)unity_mod.load_virtbase);
  if (load_module(&il2cpp_mod, LIB_IL2CPP) < 0) fatal_error("Could not load %s", LIB_IL2CPP);
  debugPrintf("[boot] loaded libil2cpp @ virtbase %p\n", (void *)il2cpp_mod.load_virtbase);
  /* hand the il2cpp exec base to the GC stop-the-world bridge in libc_shim.c so
   * our pthread_kill can ack the Boehm GC's (undeliverable) suspend/restart
   * signals via its semaphore at il2cpp+0x25936c0. */
  g_il2cpp_base = (uintptr_t)il2cpp_mod.load_virtbase;

  /* Firebase native libs are intentionally NOT loaded. The first scene's
   * FirebaseManager only advances when the managed dependency check reports
   * DependencyStatus.Available(0); on a Switch there is no Google Play Services,
   * so the real libs could never report that (they return UnavailableMissing)
   * AND libFirebaseCppApp crashes our loader in its JNI_OnLoad logging path. We
   * instead answer the SDK's native P/Invoke lookups with stubs (firebase_stub.c
   * via dlsym_fake) that make the check resolve to Available. The 4 .so files can
   * be deleted from the game folder. Firebase is cosmetic here, so stubbing it
   * costs nothing visible. */
  so_finalize(&main_mod);   so_flush_caches(&main_mod);
  so_finalize(&unity_mod);  so_flush_caches(&unity_mod);
  so_finalize(&il2cpp_mod); so_flush_caches(&il2cpp_mod);
  debugPrintf("[boot] modules finalized + flushed (canary-patch disabled)\n");

  /* Patch libunity AFTER finalize/flush -- the same point every other libunity
   * patch below runs at. so_patch_code aliases the target pages via
   * svcMapProcessMemory, but the module's segments must be finalized (mapped
   * with their final RX perms and relocated) first; doing it right after
   * load_module faulted at boot (fbstub94: stock .so on SD, PC in the patch
   * path before finalize). */
  nx_patch_libunity((uintptr_t)unity_mod.load_virtbase);
  nx_umbra_install(&unity_mod);

  /* so_patch_stack_canaries() is deliberately not called: per-thread bionic TLS
   * makes the stack guard consistent instead. */

  /* The main thread runs init_array + the engine lifecycle; give it its own
   * stable bionic TLS for the stack-protector guard (tpidr_el0+0x28). */
  static uint8_t main_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  install_bionic_tls(main_tls);

  debugPrintf("[boot] running init arrays...\n");
  so_execute_init_array(&main_mod);
  so_execute_init_array(&unity_mod);
  so_execute_init_array(&il2cpp_mod);
  so_free_temp(&main_mod); so_free_temp(&unity_mod); so_free_temp(&il2cpp_mod);
  debugPrintf("[boot] init arrays done\n");
  nx_limit_shader_cache((uintptr_t)unity_mod.load_virtbase);
  nx_set_app_path((uintptr_t)unity_mod.load_virtbase);
  nx_install_time_fix();
  if (config.engine_profiler)   /* где UnityMain/GfxWorker тратят время; выключен по умолчанию */
    nx_prof_start((uintptr_t)unity_mod.load_virtbase, unity_mod.load_size,
                  (uintptr_t)il2cpp_mod.load_virtbase, il2cpp_mod.load_size);
  nx_check_il2cpp_cache();

  /* fake JNI + our environment, then HID */
  jni_init();
  unity_environment_init(DATA_ROOT);
  android_native_update_mode();
  android_native_input_init();
  nxb_init();
  debugPrintf("[boot] jni + env + hid ready\n");

  /* re-assert the guard right before handing control to the engine, so no
   * intervening libnx/jni setup left tpidr in an unexpected state */
  install_bionic_tls(main_tls);

  /* drive the lifecycle the Java UnityPlayer would */
  extern void *fake_env, *fake_unityplayer_thiz, *fake_context_obj, *fake_surface_obj;
  extern void *fake_vm;

  /* Call libunity's real JNI_OnLoad(fake_vm) FIRST. It runs jni::Initialize(),
   * which caches the JavaVM into libunity's internal JNI manager; without this
   * ScopedJNI/LocalScope inside initJni get a NULL JNIEnv and crash. It also
   * AttachCurrentThread()s and RegisterNatives() for each subsystem (our fake
   * env handles FindClass/RegisterNatives as safe no-ops). */
  {
    typedef int (*fn_jnionload)(void *vm, void *reserved);
    fn_jnionload Unity_JNI_OnLoad = (fn_jnionload)so_try_find_addr_rx(&unity_mod, "JNI_OnLoad");
    if (!Unity_JNI_OnLoad) fatal_error("libunity.so: no exported JNI_OnLoad");
    debugPrintf("[boot] calling JNI_OnLoad(fake_vm) @%p...\n", (void *)Unity_JNI_OnLoad);
    int jver = Unity_JNI_OnLoad(fake_vm, NULL);
    debugPrintf("[boot] JNI_OnLoad returned 0x%x\n", jver);
  }

  /* Точки входа UnityPlayer берём из таблицы RegisterNatives, которую
   * JNI_OnLoad только что заполнил (jni_fake.c): у каждой сборки libunity они
   * по своим адресам. Запасной путь -- unity.<имя> в offsets.ini. */
  Unity_initJni                  = (fn_initJni) unity_native(&unity_mod, "initJni");
  Unity_nativeRecreateGfxState   = (fn_gfxstate)unity_native(&unity_mod, "nativeRecreateGfxState");
  Unity_nativeSendSurfaceChanged = (fn_v)       unity_native(&unity_mod, "nativeSendSurfaceChangedEvent");
  Unity_nativeRender             = (fn_z)       unity_native(&unity_mod, "nativeRender");
  Unity_nativeInjectEvent        = (fn_inject)  unity_native(&unity_mod, "nativeInjectEvent");
  Unity_nativeResume             = (fn_v)       unity_native(&unity_mod, "nativeResume");
  Unity_nativeFocusChanged       = (fn_vz)      unity_native(&unity_mod, "nativeFocusChanged");
  Unity_nativeDone               = (fn_z)       unity_native(&unity_mod, "nativeDone");
  Unity_nativeApplicationUnload  = (fn_v)       unity_native(&unity_mod, "nativeApplicationUnload");
  if (!Unity_initJni || !Unity_nativeRender || !Unity_nativeRecreateGfxState)
    fatal_error("UnityPlayer natives not found (initJni=%p render=%p gfx=%p).\n\n"
                "JNI_OnLoad did not register them and offsets.ini has no unity.* keys.",
                (void *)Unity_initJni, (void *)Unity_nativeRender, (void *)Unity_nativeRecreateGfxState);
  debugPrintf("[boot] entry points resolved (initJni=%p render=%p inject=%p)\n",
              (void *)Unity_initJni, (void *)Unity_nativeRender, (void *)Unity_nativeInjectEvent);

  /* Register the JavaVM with the il2cpp runtime. il2cpp caches the VM in a
   * global it later checks; without it, il2cpp logs "Java VM not initialized"
   * and every managed AndroidJNI / AndroidJavaObject call fails, hanging
   * scene load.
   *
   * We do NOT call libil2cpp's JNI_OnLoad: its first action is a log via
   * __android_log_print, whose GOT slot in libil2cpp is mis-bound (resolves to
   * a heap address -> Instruction Abort). Its only *essential* effects are two
   * global stores; the JavaVM slot comes from offsets.ini (il2cpp.javavm). */
  {
    uintptr_t b = (uintptr_t)il2cpp_mod.load_virtbase;
    /* libil2cpp's JNI_OnLoad does exactly ONE essential thing: it caches the
     * JavaVM* in a global. We replicate that single store and no more; inventing
     * a second write would scribble on an unrelated global.  (We do not call JNI_OnLoad itself:
     * its first action logs through __android_log_print, whose GOT slot in
     * libil2cpp is mis-bound under our loader -> Instruction Abort.) */
    const uint32_t vm_off = nxo_get(NXO_JAVAVM);
    if (vm_off) {
      *(void **)(b + vm_off) = fake_vm;
      debugPrintf("[boot] il2cpp JavaVM global set @il2cpp+0x%x (vm=%p)\n", (unsigned)vm_off, fake_vm);
    } else {
      debugPrintf("[boot] WARNING: %s нет в offsets.ini -- JavaVM в il2cpp не записан\n", NXO_JAVAVM);
    }
  }

  /* (Firebase native libs are not loaded; their JNI_OnLoad is neither needed nor
   * safe to call -- see the boot-time note above. The managed SDK's native calls
   * are answered by firebase_stub.c through dlsym_fake.) */

  debugPrintf("[boot] calling initJni...\n");
  Unity_initJni(fake_env, fake_unityplayer_thiz, fake_context_obj);
  debugPrintf("[boot] initJni returned; nativeRecreateGfxState...\n");
  Unity_nativeRecreateGfxState(fake_env, fake_unityplayer_thiz, 0, fake_surface_obj);
  debugPrintf("[boot] gfx state created; sendSurfaceChanged...\n");
  Unity_nativeSendSurfaceChanged(fake_env, fake_unityplayer_thiz);
  debugPrintf("[boot] surface change sent; resuming + focusing player loop\n");

  /* CRITICAL: on Android the Unity player loop only advances Update/coroutines/
   * animation when the app is RESUMED and FOCUSED. The Java UnityPlayer drives
   * this from onResume()/onWindowFocusChanged(). We had been calling only
   * initJni + gfx + render, so the engine stayed paused: it loaded the boot
   * scene and ran Awake/Start once (hence Firebase init), then rendered a frozen
   * frame forever without ticking a single Update or coroutine -- which is why
   * StartInitializer.InitUpdate was never called. Issue the resume + focus
   * transitions the lifecycle normally would before the render loop. */
  Unity_nativeResume(fake_env, fake_unityplayer_thiz);
  Unity_nativeFocusChanged(fake_env, fake_unityplayer_thiz, 1 /* hasFocus */);
  debugPrintf("[boot] resumed + focus=true; entering render loop\n");

  { extern void android_native_log_metrics(void); android_native_log_metrics(); }

  diag_thread_register(NULL, 0);
  diag_set_name(NULL, "NX_UIMain");   // the thread that drives nativeRender
  /* Watchdog re-enabled (fbstub88) with the snapshot-ordering fix: stacks are
   * now walked while the target thread is PAUSED (the fbstub86 self-crash came
   * from resuming first and walking a live stack). 6s thresholds. Its job now:
   * catch the first-present hang and dump the thread blocked in eglSwapBuffers. */
  diag_watchdog_start();

  int frame = 0;
  while (appletMainLoop() && !jni_quit_requested) {
    nx_frame_loop_begin();
    diag_frame(frame);   // heartbeat: lets the watchdog see progress (or its absence)
    extern void nxb_scope_poll(void);
    nxb_scope_poll();
    nx_time_tick();      // advance our managed-Time clock once per frame
    /* fbstub42: the engine clock is now fixed at its true source by the
     * TimeManager::Update entry hook (installed at boot, see nx_install_time_fix):
     * Update is re-driven each frame with newTime = GetTimeSinceStartup(), so all
     * deltaTime variants advance and the PreloadManager can integrate. No per-frame
     * field poking needed here. */
    android_native_update_mode();
    nxb_frame();
    android_native_feed_hid((uint8_t (*)(void*,void*,void*,int))Unity_nativeInjectEvent,
                            fake_env, fake_unityplayer_thiz);
    nx_frame_render_begin();
    if (!Unity_nativeRender(fake_env, fake_unityplayer_thiz)) break;
    nx_frame_render_end();
    if (frame == 0) {
      /* ---------------------------------------------------------------------
       * Frame-0 il2cpp work.
       *
       * Only engine-level work: GC mode, frame and render hooks, game fixes.
       * ------------------------------------------------------------------- */

      /* The matching Boehm backend pauses actual Horizon threads and
       * publishes their stacks/register roots. Keep its normal collection mode.
       * Reject an older library rather than running with unbounded garbage. */
      if (nx_gc_backend_ready()) {
        /* Matched il2cpp-api-types.h: DISABLED=0, ENABLED=1, MANUAL=2.
         * Verify the backend state instead of trusting this request. */
        enum { NX_IL2CPP_GC_MODE_ENABLED = 1 };
        typedef void (*fn_set_mode)(int);
        fn_set_mode set_mode = (fn_set_mode)so_try_find_addr_rx(&il2cpp_mod, "il2cpp_gc_set_mode");
        int (*is_disabled)(void) = (int (*)(void))so_try_find_addr_rx(&il2cpp_mod, "il2cpp_gc_is_disabled");
        if (!set_mode || !is_disabled)
          fatal_error("GC mode exports are missing; copy the matching IL2CPP library.");
        set_mode(NX_IL2CPP_GC_MODE_ENABLED);
        const int disabled = is_disabled();
        debugPrintf("[gc] native STW backend ready; requested_mode=1 disabled=%d\n", disabled);
        if (disabled) fatal_error("GC enable failed; automatic collection is still disabled.");
      } else {
        fatal_error("IL2CPP has no Horizon GC backend. Copy the complete matching update, not only the NRO.");
      }
      /* Native TimeManager is repaired at its source. Keep managed Time APIs
       * untouched: deltaTime must switch to the physics step in FixedUpdate,
       * frameCount must follow engine frames, and real time must stay monotonic. */
      {
        nx_time_bind_timescale(&il2cpp_mod);
        debugPrintf("[time] native managed accessors retained (Update/FixedUpdate aware)\n");
        /* Preserve the game's logger filter. Forcing all informational logs
         * also creates managed formatting/stack work during enemy activation.
         * Native performance/error diagnostics bypass that filter. */
        debugPrintf("[boot] game log filter preserved; native diagnostics active\n" );
        so_flush_caches(&il2cpp_mod);
        nx_diag_bind(&il2cpp_mod);
        nx_video_install_hooks(&il2cpp_mod);
        nx_apply_performance_profile();
        /* после первого кадра все модули движка уже вписали свои фазы в таблицу */
        nx_frame_install((uintptr_t)unity_mod.load_virtbase, unity_mod.load_size);
        nx_hook_install((uintptr_t)unity_mod.load_virtbase, unity_mod.load_size);
        nx_warp_install((uintptr_t)unity_mod.load_virtbase);
        { extern void nx_game_fixes_install(uintptr_t); nx_game_fixes_install((uintptr_t)il2cpp_mod.load_virtbase); }
      }
    }
    if (g_trace_exception_active &&
        __atomic_load_n(&g_port_exceptions, __ATOMIC_RELAXED) - g_trace_exception_base >= 4) {
      const uint32_t trace = nxo_get("unity.app_stacktrace");
      if (trace) ((void (*)(int, int))((uintptr_t)unity_mod.load_virtbase + trace))(4, 0);
      g_trace_exception_active = 0;
      debugPrintf("[diag] captured exception samples; further full stack walks disabled\n");
    }
    if (frame > 0) nx_fps_enforce();
    if (frame > 0 && (frame % 300) == 0) {
      nx_diag_tick(frame);
      debugPrintf("[diag] exceptions=%u\n", __atomic_load_n(&g_port_exceptions, __ATOMIC_RELAXED));
    }
    if (frame < 5 || (config.engine_profiler && (frame % 120) == 0)) debugPrintf("[boot] frame %d rendered\n", frame);
    frame++;
  }

  /* Graceful exit. The engine's render/GC/worker threads are still live here, so
   * running Unity's teardown or libnx service deinit (__libnx_exit) lets those
   * threads touch torn-down gfx/audio state and fault -- that's "closing the game
   * crashes the system". svcExitProcess() terminates the whole process (every
   * thread) atomically in the kernel BEFORE anything is torn down, so nothing can
   * race the shutdown. The system reclaims all resources and returns to HOME. */
  extern void nx_log_flush(void);
  nx_salty_shutdown();
  nx_log_flush();
  svcExitProcess();
  return 0;
}
