/* config.h -- ULTRAKILL NX: настройки загрузчика и раскладка памяти
 *
 * Загрузчик вырос из so-loader портов Unity-игр (Andy Nguyen, fgsfds и их
 * последователи, см. CREDITS.md); игра здесь -- Android-сборка ULTRAKILL
 * (Unity 2022.3, IL2CPP), альбомная.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// mmap arena. Unity reserves big aligned pools by over-mmapping a larger region
// then munmapping the unaligned head/tail to keep an aligned middle. A plain
// malloc/free-per-mmap frees the WHOLE block when the head is trimmed and
// corrupts the kept middle -- the TLSF "next_free == NULL" allocator crash. We
// instead back anonymous mmaps from a dedicated aligned arena with a per-page
// used-bitmap (carved in __libnx_initheap) so sub-range munmap frees exactly the
// trimmed pages. Big requests are handed back aligned so Unity only ever trims
// the (reusable) tail.
#define MMAP_ARENA_ALIGN    ((size_t)64 * 1024 * 1024)    // регионы Unity -- по 64 МБ: patch.unity.region* меняет зернистость 256 -> 64 МБ
#define MMAP_ARENA_RESERVE  ((size_t)1792 * 1024 * 1024)  // heap-backed cap: 28 blocks of 64 MB

// Stack-region overcommit (OC) arena (see libc_shim.c). svcMapMemory can only
// alias into the 2048MB stack region, so we hold the big PROT_NONE reservations
// in a stack-region window and back the committed pages from a heap pool.
#define OC_WINDOW_BYTES     ((size_t)1536 * 1024 * 1024)  // 24x64MB blocks of cheap PROT_NONE reservation in the stack hole
/* Commit pool: real memory backing the pages the engine actually touches.
 * A NULL from malloc/mmap under pressure, dereferenced unchecked, shows up as a
 * crash right after an allocation; this pool is what runs out first. If you see
 * "[oc] commit-pool EXHAUSTED" in debug.log, raise this and drop
 * OVERCOMMIT_HEAP_MB by the same amount -- the log names which side ran out. */
#define OC_POOL_BYTES       ((size_t) 640 * 1024 * 1024)

// Overcommit (alias-region) mode: Unity reserves multi-GB of pools with
// mmap(PROT_NONE) and commits only the sub-ranges it touches via mprotect. On a
// fully-backed heap arena every reservation costs real RAM and starves il2cpp.
// When svcMapPhysicalMemory + a large alias region are available we instead
// reserve a big *virtual* window there (PROT_NONE costs only address space) and
// commit physical pages on demand -- true overcommit, matching Android. This is
// the virtual window size (cheap; the alias region is tens of GB).
#define MMAP_VIRT_RESERVE   ((size_t)6144 * 1024 * 1024)  // 6 GB virtual reservation window
// Heap kept (svcSetHeapSize) in overcommit mode: newlib malloc + .so load zone.
// The rest of the physical limit is freed for on-demand commits. 2MB-aligned.
// r89: GPU BOs share newlib's heap. r88 hit GL OOM with ~1.2GB alias
// commits while total RAM still had room; reserve more heap for large BOs.
#define OVERCOMMIT_HEAP_MB  800u

/* Папка игры на SD: sdmc:/switch/ultrakill. Unity грузит обычную тройку
 * libmain.so -> libunity.so -> libil2cpp.so (main.c), Burst не используется. */
#define GAME_FOLDER  "ultrakill"

/* Android package identity: getPackageName() и PackageInfo.versionName, то есть
 * Application.identifier и Application.version. Пути /data/data/<pkg>/ порт всё
 * равно переводит на SD, но пустая версия приложения у Unity приводит к зависанию
 * на старте, поэтому строка нужна непустая. */
#define GAME_PACKAGE       "com.ultrakillnx.port"      /* задаётся нами в Player Settings сборки */
#define GAME_VERSION_NAME  "4.1"                       /* bundleVersion из PlayerSettings игры */

#define CONFIG_NAME "config.txt"

/* Returned for getenv("HOME")/getpwuid()->pw_dir: Unity computes a home/cache
 * dir during engine init, and our env has no HOME and no passwd db. It is also
 * the il2cpp data dir and the root of persistentDataPath -- saves live under it,
 * so moving it between builds orphans them. */
#define GAME_HOME   "sdmc:/switch/" GAME_FOLDER

// file logging (debug.log) for on-hardware debugging
#define DEBUG_LOG 1
/* The render surface the engine believes it has (1280x720 handheld, 1920x1080
 * docked with docked_1080p). android_native_update_mode() keeps these in step
 * with the real mode; imports.c uses them as the fallback when eglQuerySurface
 * reports 0x0, which mesa on this platform does. */
extern int screen_width;
extern int screen_height;

/* ---------------------------- Display metrics ----------------------------
 * Reported through DisplayMetrics. 237 dpi is the real handheld panel: 1280x720
 * across 6.2 inches diagonal. Docked the reported dpi is scaled with the render
 * size, so the derived physical size stays constant and the game sees one device
 * whichever mode it is in. nx_screen_dpi() does that. */
#define SCREEN_DPI_HANDHELD 237

typedef struct {
  int   portrait;      /* всегда 0: ULTRAKILL альбомная, в файл не пишется */
  int   docked_1080p;  /* 1 = в доке рендер 1920x1080, 0 = 1280x720 (по умолчанию, быстрее) */
  int   gyro;          /* 1 = гироприцел включён (отдаётся в C# через nx_bridge) */
  float gyro_sens;     /* множитель чувствительности гироскопа, 1.0 = по умолчанию */
  int   gyro_invert_x; /* 1 = развернуть горизонталь гироскопа (по умолчанию) */
  int   gyro_invert_y; /* 1 = инвертировать вертикаль гироскопа */
  int   shader_chunks; /* bounded decompressed shader cache, 16 x 16 MiB */
  int   performance_mode; /* 60 FPS target, lower texture/lighting quality */
  int   render_height; /* software render resolution; native display stays 720p */
  float physics_max_dt; /* Time.maximumDeltaTime, с: потолок шагов физики за кадр */
  int   engine_profiler; /* 1 = сэмплирующий профайлер UnityMain/GfxWorker в журнал (default 0) */
  int   frame_stats; /* 1 = [frame]: фазы кадра в журнал (default); старое 2 переводится в 1 */
  int   swap_interval; /* 0 = показ без ожидания развёртки, 1 = всегда ждать, 2 = по FPS (default) */
  int   collision_reuse; /* 1 = Physics.reuseCollisionCallbacks (меньше мусора для GC, default) */
  int   worker_core0; /* 1 = главный поток 0x2b, Job.Worker и на ядре 0 (default) */
  int   core3; /* 1 = потоки задач и фон и на ядре 3, если его открыл NPDM (default) */
  int   gl_program_cache; /* 0 = кэш выкл., 1 = бинарники Unity + код nvc0 (default), 2 = + GLSL-кэш Mesa */
  int   fps_limit; /* 0 = лимит из настроек игры (default; «выкл» = без лимита), 30..1000 = свой */
  int   autoaim_every; /* поиск цели автоприцела раз в N кадров (1 = каждый, default 2) */
  int   sky_every; /* небо Лимбо: 1 = каждый кадр, 2 = через кадр, 0 = через кадр, пока FPS ниже 90 (default) */
  int   physics_solver; /* итерации решателя PhysX для новых тел, 0 = как в проекте (6); default 4 */
  int   physics_sync; /* 1 = синхронизация физики перед фазами скриптов вместо autoSyncTransforms (default) */
  int   reproject; /* 1 = вставка кадров, повёрнутых по камере, при FPS ниже 60 (прототип, default) */
  int   ui_language; /* 1 = English (default), 0 = system, 2..14 = translations */
  int   ui_language_revision; /* one-time migration of the old automatic default */
    int   saltynx; /* optional direct NX-FPS shared-memory producer */
  int loading_boost; /* Horizon FastLoad during startup / scene loads, timeout 60s */
  int   fsr; /* experimental spatial EASU+RCAS, requires managed bridge + bundle */
  int   fsr3; /* experimental temporal FSR 3.1, selected independently of filter enable */
  int   fg_probe; /* one-shot GLES/deko bring-up callback, NOT frame generation */
  int   frame_generation;
  int   spawn_warmup;
  float fsr_sharpness; /* RCAS attenuation stops: 0 strongest, 2 weakest */
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
