/* Streaming NXVID001 player: indexed JPEG on a worker, PCM on Unity's mixer.
 * Encoded files are prepared offline with tools/prepare_video.py. */
#include <switch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <turbojpeg.h>
#include "nx_video.h"
#include "nx_offsets.h"
#include "nx_gameroot.h"
#include "util.h"

#define PLAYERS 16
typedef struct {
  char magic[8];
  uint32_t width, height, fps_num, fps_den, frames, sample_rate, channels, reserved;
  uint64_t index_offset, audio_offset, audio_frames;
} VideoHeader;
_Static_assert(sizeof(VideoHeader) == 64, "NX video header ABI");
_Static_assert(sizeof(NxbVideoInfo) == 40, "NX video info ABI");

typedef struct {
  int handle, state, error, wanted, decoded, front, delivered, stopping;
  uintptr_t player;
  uint32_t texture;
  VideoHeader h;
  uint64_t *offsets;
  FILE *file, *audio;
  uint8_t *pixels[2], *delivery, *jpeg;
  size_t pixel_bytes, jpeg_capacity;
  tjhandle decoder;
  Thread worker;
  int worker_started;
  Mutex frame_lock;
  UEvent wake;
  double position, speed;
  uint64_t start_tick;
} Video;
static Video videos[PLAYERS];
static unsigned generations[PLAYERS];
static Mutex registry_lock;

static Video *lookup(int handle) {
  unsigned slot = (unsigned)handle & 0xff;
  if (!slot || slot > PLAYERS) return NULL;
  Video *v = &videos[slot-1];
  return v->handle == handle && v->file ? v : NULL;
}
static Video *for_object(void *managed) {
  if (!managed) return NULL;
  /* UnityEngine.Object has one field, m_CachedPtr, after Il2CppObject's header.
   * Verified against this project's generated C++ and preserved by link.xml. */
  uintptr_t player = *(uintptr_t *)((char *)managed + 16);
  for (unsigned i = 0; i < PLAYERS; ++i)
    if (videos[i].file && videos[i].player == player) return &videos[i];
  return NULL;
}
static double duration(Video *v) { return (double)v->h.frames * v->h.fps_den / v->h.fps_num; }
static double position(Video *v) {
  double p = v->position;
  if (v->state == 1)
    p += (double)armTicksToNs(armGetSystemTick()-v->start_tick) / 1e9 * v->speed;
  const double end = duration(v);
  return p < 0 ? 0 : p > end ? end : p;
}
static int prepared(Video *v) {
  mutexLock(&v->frame_lock);
  int value = v->decoded >= 0 && !v->error;
  mutexUnlock(&v->frame_lock);
  return value;
}
static void control(Video *v, int action, double value) {
  if (action == 1) { /* play/resume */
    if (v->state != 1) {
      if (position(v) >= duration(v)) v->position = 0;
      v->start_tick = armGetSystemTick(); v->state = 1;
    }
  } else if (action == 2) { v->position = position(v); v->state = 2; }
  else if (action == 0) { v->position = 0; v->state = 0; }
  else if (action == 3) {
    v->position = isfinite(value) ? fmax(0, fmin(duration(v), value)) : 0;
    v->start_tick = armGetSystemTick();
  } else if (action == 4 && isfinite(value) && value > 0 && value <= 4) {
    v->position = position(v); v->start_tick = armGetSystemTick(); v->speed = value;
  }
}

static void decode_worker(void *arg) {
  Video *v = arg;
  for (;;) {
    waitSingle(waiterForUEvent(&v->wake), UINT64_MAX);
    mutexLock(&v->frame_lock);
    int stop = v->stopping, wanted = v->wanted, back = 1-v->front;
    int decoded = v->decoded;
    mutexUnlock(&v->frame_lock);
    if (stop) return;
    if (wanted == decoded || wanted < 0 || (uint32_t)wanted >= v->h.frames) continue;
    uint64_t offset = v->offsets[wanted], length = v->offsets[wanted+1]-offset;
    int ok = length <= v->jpeg_capacity && !fseeko(v->file, (off_t)offset, SEEK_SET) &&
             fread(v->jpeg, 1, (size_t)length, v->file) == length;
    int w = 0, h = 0, sub = 0, color = 0;
    if (ok) ok = !tjDecompressHeader3(v->decoder, v->jpeg, (unsigned long)length,
                                    &w, &h, &sub, &color) &&
                 w == (int)v->h.width && h == (int)v->h.height;
    if (ok) ok = !tjDecompress2(v->decoder, v->jpeg, (unsigned long)length,
                 v->pixels[back], w, 0, h, TJPF_RGB, TJFLAG_FASTDCT | TJFLAG_BOTTOMUP);
    mutexLock(&v->frame_lock);
    if (ok) { v->front = back; v->decoded = wanted; }
    else { v->error = 1; debugPrintf("[video] decode FAIL handle=%d frame=%d\n", v->handle, wanted); }
    int again = !v->stopping && v->wanted != wanted;
    mutexUnlock(&v->frame_lock);
    if (again) ueventSignal(&v->wake);
  }
}

static void release_video(Video *v) {
  if (v->worker_started) {
    mutexLock(&v->frame_lock); v->stopping = 1; mutexUnlock(&v->frame_lock);
    ueventSignal(&v->wake); threadWaitForExit(&v->worker); threadClose(&v->worker);
  }
  if (v->decoder) tjDestroy(v->decoder);
  if (v->file) fclose(v->file);
  if (v->audio) fclose(v->audio);
  free(v->offsets); free(v->pixels[0]); free(v->pixels[1]);
  free(v->delivery); free(v->jpeg);
  memset(v, 0, sizeof *v);
}

int nxb_video_open(const char *name, uintptr_t player, int playing, NxbVideoInfo *info) {
  if (!name || !*name || !player || !info || strlen(name) > 120 ||
      strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) return 0;
  mutexLock(&registry_lock);
  int slot;
  for (slot = 0; slot < PLAYERS && videos[slot].file; ++slot) {}
  if (slot == PLAYERS) { mutexUnlock(&registry_lock); return 0; }
  Video *v = &videos[slot];
  memset(v, 0, sizeof *v);
  char path[512], relative[180];
  snprintf(relative, sizeof relative, "assets/VideosNX/%s.nxv", name);
  game_path(path, sizeof path, relative);
  v->file = fopen(path, "rb");
  if (!v->file) goto failed;
  if (fread(&v->h, 1, sizeof v->h, v->file) != sizeof v->h) goto failed;
  VideoHeader *h = &v->h;
  if (memcmp(h->magic, "NXVID001", 8) || !h->width || h->width > 1280 ||
      !h->height || h->height > 720 || !h->frames || h->frames > 108000 ||
      !h->fps_num || !h->fps_den || h->channels > 2 ||
      (h->channels && (!h->sample_rate || h->sample_rate > 48000))) goto failed;
  if (fseeko(v->file, 0, SEEK_END)) goto failed;
  uint64_t size = (uint64_t)ftello(v->file);
  uint64_t index_size = ((uint64_t)h->frames+1)*8;
  if (h->index_offset < sizeof *h || h->index_offset > size ||
      index_size > size-h->index_offset || h->audio_offset != h->index_offset+index_size ||
      h->audio_frames > (size-h->audio_offset) / (h->channels ? h->channels*4 : 1)) goto failed;
  v->offsets = malloc((size_t)index_size);
  if (!v->offsets || fseeko(v->file, (off_t)h->index_offset, SEEK_SET) ||
      fread(v->offsets, 1, (size_t)index_size, v->file) != index_size) goto failed;
  uint64_t maximum = 0;
  for (unsigned i = 0; i < h->frames; ++i) {
    uint64_t a = v->offsets[i], b = v->offsets[i+1];
    if (a < sizeof *h || b <= a || b > h->index_offset || b-a > 8*1024*1024) goto failed;
    if (b-a > maximum) maximum = b-a;
  }
  v->jpeg_capacity = (size_t)maximum;
  v->pixel_bytes = (size_t)h->width*h->height*3;
  v->pixels[0] = malloc(v->pixel_bytes); v->pixels[1] = malloc(v->pixel_bytes);
  v->delivery = malloc(v->pixel_bytes); v->jpeg = malloc(v->jpeg_capacity);
  v->decoder = tjInitDecompress();
  if (!v->pixels[0] || !v->pixels[1] || !v->delivery || !v->jpeg || !v->decoder) goto failed;
  if (h->channels) {
    v->audio = fopen(path, "rb");
    if (!v->audio || fseeko(v->audio, (off_t)h->audio_offset, SEEK_SET)) goto failed;
  }
  unsigned generation = ++generations[slot];
  if (!generation || generation > 0x7fffff) generation = generations[slot] = 1;
  v->handle = (int)((generation<<8) | (unsigned)(slot+1));
  v->player = player; v->speed = 1; v->decoded = v->delivered = -1;
  v->start_tick = armGetSystemTick(); v->state = playing ? 1 : 0;
  ueventCreate(&v->wake, true);
  if (R_FAILED(threadCreate(&v->worker, decode_worker, v, NULL, 0x10000, 0x2f, -2))) goto failed;
  v->worker_started = 1;
  if (R_FAILED(threadStart(&v->worker))) { v->worker_started = 0; threadClose(&v->worker); goto failed; }
  memcpy(info, &h->width, 32); info->audio_frames = h->audio_frames;
  ueventSignal(&v->wake);
  debugPrintf("[video] opened %s: %ux%u frames=%u audio=%u/%u handle=%d\n",
      name, h->width, h->height, h->frames, h->sample_rate, h->channels, v->handle);
  int handle = v->handle; mutexUnlock(&registry_lock); return handle;
failed:
  debugPrintf("[video] open FAIL: %s\n", name);
  release_video(v); mutexUnlock(&registry_lock); return 0;
}

void nxb_video_close(int handle) {
  mutexLock(&registry_lock); Video *v = lookup(handle);
  if (v) release_video(v);
  mutexUnlock(&registry_lock);
}
void *nxb_video_frame(int handle, int wanted, int *actual) {
  void *result = NULL;
  if (actual) *actual = -1;
  mutexLock(&registry_lock); Video *v = lookup(handle);
  if (v) {
    if (wanted < 0) wanted = 0;
    if ((uint32_t)wanted >= v->h.frames) wanted = (int)v->h.frames-1;
    mutexLock(&v->frame_lock);
    int request = v->wanted != wanted;
    v->wanted = wanted;
    if (v->decoded >= 0) {
      if (v->delivered != v->decoded) {
        memcpy(v->delivery, v->pixels[v->front], v->pixel_bytes);
        v->delivered = v->decoded;
      }
      if (actual) *actual = v->delivered;
      result = v->delivery;
    }
    mutexUnlock(&v->frame_lock);
    if (request) ueventSignal(&v->wake);
  }
  mutexUnlock(&registry_lock); return result;
}
int nxb_video_audio(int handle, float *samples, int count) {
  if (!samples || count <= 0 || count > 65536) return 0;
  memset(samples, 0, (size_t)count*sizeof(float));
  mutexLock(&registry_lock); Video *v = lookup(handle);
  int got = v && v->audio && v->state == 1 ? (int)fread(samples, sizeof(float), (size_t)count, v->audio) : 0;
  mutexUnlock(&registry_lock); return got;
}
void nxb_video_audio_seek(int handle, int frame) {
  mutexLock(&registry_lock); Video *v = lookup(handle);
  if (v && v->audio) {
    uint64_t f = frame < 0 ? 0 : (uint64_t)frame;
    if (f > v->h.audio_frames) f = v->h.audio_frames;
    fseeko(v->audio, (off_t)(v->h.audio_offset+f*v->h.channels*4), SEEK_SET);
  }
  mutexUnlock(&registry_lock);
}
double nxb_video_time(int handle) {
  mutexLock(&registry_lock); Video *v = lookup(handle); double p = v ? position(v) : 0;
  mutexUnlock(&registry_lock); return p;
}
int nxb_video_status(int handle) {
  mutexLock(&registry_lock); Video *v = lookup(handle);
  int state = -1;
  if (v) {
    mutexLock(&v->frame_lock); state = v->error ? -1 : v->state; mutexUnlock(&v->frame_lock);
  }
  mutexUnlock(&registry_lock); return state;
}
void nxb_video_control(int handle, int action, double value) {
  mutexLock(&registry_lock); Video *v = lookup(handle);
  if (v) control(v, action, value);
  mutexUnlock(&registry_lock);
}
void nxb_video_texture(int handle, uint32_t gc_handle) {
  mutexLock(&registry_lock); Video *v = lookup(handle);
  if (v) v->texture = gc_handle;
  mutexUnlock(&registry_lock);
}

/* The real Android decoder does not exist on Horizon. Returning NULL is the
 * documented allocation failure, which Unity handles; leaving a poisoned PLT
 * entry caused the instruction abort in r19 before our managed replacement ran. */
void *nx_media_extractor_unavailable(void) {
  static int logged;
  if (!logged++) debugPrintf("[video] Android extractor unavailable; using NX software player\n");
  return NULL;
}

/* Managed VideoPlayer API redirects are installed below. Native VideoPlayer is
 * disabled by NxVideoPlayer after binding; non-NX instances use genuine icalls. */
typedef void *(*ResolveIcall)(const char *);
static void *(*gc_target)(uint32_t);
#define GETTER(name, type, expression) \
  static type (*original_##name)(void *); \
  static type hook_##name(void *obj, void *method) { \
    (void)method; mutexLock(&registry_lock); Video *v = for_object(obj); \
    if (v) { type result = (expression); mutexUnlock(&registry_lock); return result; } \
    mutexUnlock(&registry_lock); return original_##name(obj); \
  }
GETTER(get_isPlaying, uint8_t, v->state == 1 && position(v) < duration(v))
GETTER(get_isPaused, uint8_t, v->state == 2)
GETTER(get_isPrepared, uint8_t, prepared(v))
GETTER(get_time, double, position(v))
GETTER(get_length, double, duration(v))
GETTER(get_frame, int64_t, (int64_t)(position(v)*v->h.fps_num/v->h.fps_den))
GETTER(get_frameRate, float, (float)v->h.fps_num/v->h.fps_den)
GETTER(get_frameCount, uint64_t, v->h.frames)
GETTER(get_width, uint32_t, v->h.width)
GETTER(get_height, uint32_t, v->h.height)
GETTER(get_texture, void *, v->texture && gc_target ? gc_target(v->texture) : NULL)
#define COMMAND(name, action) \
  static void (*original_##name)(void *); \
  static void hook_##name(void *obj, void *method) { \
    (void)method; mutexLock(&registry_lock); Video *v = for_object(obj); \
    if (v) { control(v, action, 0); mutexUnlock(&registry_lock); return; } \
    mutexUnlock(&registry_lock); original_##name(obj); \
  }
COMMAND(Play, 1)
COMMAND(Pause, 2)
COMMAND(Stop, 0)
static void (*original_Prepare)(void *);
static void hook_Prepare(void *obj, void *method) {
  (void)method; mutexLock(&registry_lock); Video *v = for_object(obj);
  mutexUnlock(&registry_lock); if (!v) original_Prepare(obj);
}
static void (*original_set_time)(void *, double);
static void hook_set_time(void *obj, double value, void *method) {
  (void)method; mutexLock(&registry_lock); Video *v = for_object(obj);
  if (v) { control(v, 3, value); mutexUnlock(&registry_lock); return; }
  mutexUnlock(&registry_lock); original_set_time(obj, value);
}
static void (*original_set_frame)(void *, int64_t);
static void hook_set_frame(void *obj, int64_t value, void *method) {
  (void)method; mutexLock(&registry_lock); Video *v = for_object(obj);
  if (v) { control(v, 3, (double)value*v->h.fps_den/v->h.fps_num); mutexUnlock(&registry_lock); return; }
  mutexUnlock(&registry_lock); original_set_frame(obj, value);
}

void nx_video_install_hooks(so_module *mod) {
  ResolveIcall resolve = (ResolveIcall)so_try_find_addr_rx(mod, "il2cpp_resolve_icall");
  gc_target = (void *(*)(uint32_t))so_try_find_addr_rx(mod, "il2cpp_gchandle_get_target");
  if (!resolve || !gc_target) return;
  unsigned installed = 0;
#define INSTALL(name, signature) do { \
    char key[64]; snprintf(key, sizeof key, "il2cpp.video_%s", #name); \
    uint32_t offset = nxo_get(key); \
    original_##name = (void *)resolve("UnityEngine.Video.VideoPlayer::" #name signature); \
    if (offset && original_##name) { \
      uintptr_t fn = (uintptr_t)&hook_##name; \
      uint32_t stub[4] = { 0x58000050, 0xd61f0200, (uint32_t)fn, (uint32_t)(fn>>32) }; \
      so_patch_code((void *)((uintptr_t)mod->load_virtbase+offset), stub, sizeof stub); \
      ++installed; \
    } else debugPrintf("[video] hook missing: %s\n", key); \
  } while (0)
  INSTALL(get_isPlaying, "()"); INSTALL(get_isPaused, "()"); INSTALL(get_isPrepared, "()");
  INSTALL(get_time, "()"); INSTALL(get_length, "()"); INSTALL(get_frame, "()");
  INSTALL(get_frameRate, "()"); INSTALL(get_frameCount, "()");
  INSTALL(get_width, "()"); INSTALL(get_height, "()"); INSTALL(get_texture, "()");
  INSTALL(Play, "()"); INSTALL(Pause, "()"); INSTALL(Stop, "()"); INSTALL(Prepare, "()");
  INSTALL(set_time, "(System.Double)"); INSTALL(set_frame, "(System.Int64)");
  so_flush_caches(mod);
  debugPrintf("[video] installed %u/17 managed VideoPlayer hooks\n", installed);
#undef INSTALL
}
