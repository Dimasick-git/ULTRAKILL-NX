/* config.h -- Papers, Please (Android/Unity) Switch wrapper configuration
 *
 * Forked from the Zookeeper DX wrapper, which is the right base here: Papers,
 * Please is the SAME Unity minor version (2022.3.62f2) and, like Zookeeper, is
 * a PORTRAIT game -- so the TATE rotation path and the pointer mapping that
 * goes with it are inherited rather than reinvented.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// The engine + libc++ + huge mvgl page cache need a generous newlib heap; the
// rest of system memory is handed to the .so loader (see __libnx_initheap).
#define MEMORY_MB 768

// mmap arena. Unity reserves big *256MB-aligned* pools by over-mmapping a larger
// region then munmapping the unaligned head/tail to keep an aligned middle. A
// plain malloc/free-per-mmap frees the WHOLE block when the head is trimmed and
// corrupts the kept middle -- which is the TLSF "next_free == NULL" allocator
// crash. We instead back anonymous mmaps from a dedicated, 256MB-aligned arena
// with a per-page used-bitmap (carved in __libnx_initheap) so sub-range munmap
// frees exactly the trimmed pages. Big requests are handed back 256MB-aligned so
// Unity only ever trims the (reusable) tail.
#define MMAP_ARENA_ALIGN    ((size_t)64 * 1024 * 1024)    // регионы Unity -- по 64 МБ: patch.unity.region* меняет зернистость 256 -> 64 МБ
#define MMAP_ARENA_RESERVE  ((size_t)1792 * 1024 * 1024)  // heap-backed cap; now 28x64MB blocks (was 7x256MB), same bytes

// Stack-region overcommit (OC) arena (see libc_shim.c). svcMapMemory can only
// alias into the 2048MB stack region, so we hold the big PROT_NONE reservations
// in a stack-region window and back the (~80MB) committed pages from a small
// heap bump-pool. Heap-backed arena (1792MB) + OC window (1280MB) = 12x256MB
// blocks of reservation address space for Unity, but only ~80MB real commits.
#define OC_WINDOW_BYTES     ((size_t)1536 * 1024 * 1024)  // 24x64MB blocks of cheap PROT_NONE reservation in the stack hole
/* Commit-pool: real memory backing the pages the engine actually touches.
 *
 * Raised from the base's 256 MB, which was sized for ITS game ("~80MB observed").
 * That number does not transfer. Papers, Please decodes its art out of a
 * TextAsset blob into uncompressed RGBA Image buffers and keeps them in its own
 * cache (HostUnity.CachedTexture, evicted by lastUsedFrame) -- so its working set
 * GROWS during a session as more documents, stamps and portraits get touched,
 * rather than sitting flat the way a pre-baked scene does.
 *
 * That is consistent with the reported failure: boots and plays fine, then dies
 * partway into a session with a NULL dereference immediately after an allocation
 * call returned. malloc/mmap handing back NULL under pressure, dereferenced
 * unchecked, is exactly that crash shape.
 *
 * 640 MB against a 608 MB newlib heap fits the same physical budget the base
 * used; we are moving headroom from "reserved but unused" into the pool that
 * actually ran out. If you see "[oc] commit-pool EXHAUSTED" in debug.log, raise
 * this further and drop OVERCOMMIT_HEAP_MB by the same amount -- the log names
 * which side ran out, so you never have to guess. */
#define OC_POOL_BYTES       ((size_t) 640 * 1024 * 1024)

// Overcommit (alias-region) mode: Unity reserves multi-GB of 256MB pools with
// mmap(PROT_NONE) and commits only the sub-ranges it touches via mprotect. On a
// fully-backed heap arena every reservation costs real RAM (9 pools == the whole
// arena, starving il2cpp). When svcMapPhysicalMemory + a large alias region are
// available we instead reserve a big *virtual* window there (PROT_NONE costs only
// address space) and commit physical pages on demand -- true overcommit, matching
// Android. This is the virtual window size (cheap; the alias region is tens of GB).
#define MMAP_VIRT_RESERVE   ((size_t)6144 * 1024 * 1024)  // 6 GB virtual reservation window
// Heap kept (svcSetHeapSize) in overcommit mode: newlib malloc + .so load zone.
// The rest of the physical limit is freed for on-demand commits. 2MB-aligned.
#define OVERCOMMIT_HEAP_MB  608u

/* Papers, Please ships the standard modern Unity trio: libmain.so dlopens
 * libunity.so which dlopens libil2cpp.so. There is NO lib_burst_generated.so --
 * the game uses no Burst jobs, so unlike the PvZ Fusion and CloverPit ports you
 * do not need to supply one. (SO_NAME is inherited from the Chaos Rings 3
 * lineage and is unused on this path; main.c loads the trio by name.) */
#define GAME_FOLDER  "ultrakill"

/* ---------------------- Android package identity -------------------------
 * *** VERIFY THESE TWO AGAINST YOUR APK BEFORE RELEASE. ***
 *
 * They are returned by getPackageName() and by the PackageInfo.versionName
 * getter, i.e. they become Application.identifier and Application.version.
 * They could NOT be recovered from the .so files you supplied -- the package
 * name lives in AndroidManifest.xml, not in libil2cpp -- so the values below
 * are placeholders, not derived facts. The Zookeeper base had its own game's
 * strings hardcoded in two separate files; they are centralised here so there
 * is exactly one place to fix.
 *
 * GAME_VERSION_NAME is now REAL: assets/Version.txt in the shipped data reads
 * 1.4.15.128. Only the package name is still a guess.
 *
 * To get it:
 *     aapt dump badging YourGame.apk | head -1
 * or unzip the APK and read the `package=` / `versionName=` attributes from
 * AndroidManifest.xml, or read the id= parameter off the Play Store URL.
 *
 * How much does it matter? Not much here: Unity uses the package name to build
 * /data/data/<pkg>/... paths that this port overrides to sdmc anyway, and
 * Papers, Please's own Haxe-side code never reads either value. But the
 * Zookeeper base hit a real boot stall from a BLANK Application.version, so a
 * plausible non-empty version string is worth keeping. */
#define GAME_PACKAGE       "com.ultrakillnx.port"      /* задаётся нами в Player Settings сборки */
#define GAME_VERSION_NAME  "4.1"                       /* bundleVersion из PlayerSettings игры */
#define SO_NAME      "libunity.so"
#define SO_CPP_NAME  "libil2cpp.so"

// the main game archive (an APK asset). The "10007" is the APK versionCode.
#define MAIN_MVGL    "main.10007.android.mvgl"

#define CONFIG_NAME "config.txt"
#define LOG_NAME    "sdmc:/switch/" GAME_FOLDER "/debug.log"

// Returned for getenv("HOME")/getpwuid()->pw_dir. Unity computes a home/cache dir
// during engine init; our env has no HOME and no passwd db, so point it at the
// (writable) game data root instead of letting it deref a NULL passwd.
/* Returned for getenv("HOME")/getpwuid()->pw_dir, used as the il2cpp data dir,
 * and -- importantly for this game -- the root of the save location. Papers,
 * Please does NOT use PlayerPrefs: HostUnity.UnityPlatformDisk writes real files
 * via getPersistentString/setPersistentString into a saveDir under the
 * persistent data path. Move this between builds and you orphan saves. */
#define GAME_HOME   "sdmc:/switch/" GAME_FOLDER

// flip to 1 (and build) to get file logging (debug.log) for on-hardware debugging
#define DEBUG_LOG 1   /* пока порт не заведён на железе, журнал нужен всегда */
/* The render surface the engine believes it has, in portrait orientation
 * (720x1280 handheld, 1080x1920 docked). android_native_update_mode() keeps
 * these in step with the real mode; imports.c uses them as the fallback when
 * eglQuerySurface reports 0x0, which mesa on this platform does. */
extern int screen_width;
extern int screen_height;

/* ---------------------------- Display metrics ----------------------------
 * Reported through DisplayMetrics, and load-bearing: Papers, Please picks its
 * layout from it (HostUnity.Monitor.detectPlatformKind /
 * getSuggestedHandheldPlatformKind read dpi, deviceWidthInInches,
 * deviceHeightInInches and aspect). Not a config option -- the Switch is one
 * known panel, so there is nothing for the player to tune.
 *
 * 237 dpi is the real handheld panel: 1280x720 across 6.2 inches diagonal.
 *
 * The docked case is NOT simply the same number. Docked we render 1080x1920
 * instead of 720x1280, and if the dpi stayed at 237 the game would compute a
 * 4.6" x 8.1" device -- a tablet -- and could switch layout on docking. Scaling
 * the reported dpi by the same 1.5x keeps the DERIVED PHYSICAL SIZE constant, so
 * the game sees one device whichever mode it is in. nx_screen_dpi() does that. */
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
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
