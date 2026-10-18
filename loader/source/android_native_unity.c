/* android_native_unity.c -- the 27 NDK symbols libunity.so imports, for the
 * PAPERS, PLEASE Switch port. Unity is NOT a NativeActivity, so unlike cr3_nx's
 * android_native.c there is no ANativeActivity glue / android_main / AInputQueue
 * here: the engine is driven by the JNI-registered natives (see main.c). We only
 * provide the raw NDK functions libunity calls directly:
 *
 *   ANativeWindow_acquire/_release/_fromSurface/_setBuffersGeometry/
 *                _getWidth/_getHeight/_getFormat      -> libnx NWindow
 *   ALooper_prepare/_acquire/_release/_pollOnce/_wake/_forThread
 *                                                     -> condvar wait/wake
 *   ASensorManager_ , ASensorEventQueue_ , ASensor_   -> "no sensors"
 *
 * IMPORTANT context-ownership note: the engine creates its OWN EGL context from
 * the ANativeWindow (cr3_nx's main.c creates none). The host must NOT create an
 * SDL_GL / EGL context. Use SDL for audio + HID only. Delete the
 * SDL_GL_SetAttribute/SDL_GL_CreateContext/SDL_GL_SwapWindow calls from the
 * earlier main_skeleton.c; the engine calls eglSwapBuffers itself.
 *
 * Needs devkitA64 + libnx (switch.h) + switch-mesa. Not host-compilable.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <switch.h>
#include <GLES3/gl3.h> /* docked cursor overlay */
#include "util.h"   /* debugPrintf */
#include "config.h" /* config.portrait */
#include "nx_gameroot.h" /* game_root() -- runtime folder resolution */

#ifndef AWINDOW_FORMAT_RGBA_8888
#define AWINDOW_FORMAT_RGBA_8888 1
#endif

/* opaque NDK types -> concrete libnx instances */
typedef struct ANativeWindow ANativeWindow;     /* == NWindow* at runtime */
typedef struct ALooper       ALooper;

/* ==========================================================================
 * dock-aware screen state (also read by unity_jni.c's Display getters)
 * ========================================================================== */
static u32 g_w = 720, g_h = 1280;   /* fbstub45 PORTRAIT (stable) */
/* The resolution Unity BELIEVES it has = the last size it requested via
 * setBuffersGeometry (its SetResolution 640x1137). We reject the real NWindow
 * resize (kept at g_w x g_h) but Unity maps ALL input into this believed space,
 * so injected touch/cursor coords must be SCALED from framebuffer space
 * (g_w x g_h) into it -- otherwise the right/bottom ~11% lands beyond Unity's
 * screen and is dead (the "can't tap the far right" bug, on touch AND cursor).
 * 0 => not set yet, use g_w/g_h. */
static int g_input_w = 0, g_input_h = 0;
static inline float nxp_scale_x(void){ return (float)(g_input_w > 0 ? g_input_w : (int)g_w) / (float)g_w; }
static inline float nxp_scale_y(void){ return (float)(g_input_h > 0 ? g_input_h : (int)g_h) / (float)g_h; }

/* Two sizes, and the distinction is the whole trick.
 *
 *   g_win_*  the REAL window. Always the landscape panel, because that is the
 *            only thing the compositor accepts once rendering has started
 *            (nwindowSetDimensions fails afterwards -- rc=0xf59 on hardware).
 *   g_w/g_h  what the ENGINE is told. Portrait, so the game uses its portrait
 *            layout, which is the only layout the phone build ships.
 *
 * The gap between them is closed in GL by pp_tate.c: the engine renders into a
 * portrait FBO and we blit it rotated into the landscape window at present time.
 * That is also why the buffer transform is 0 below -- the rotation happens where
 * we control the pixels, not in the compositor. */
static u32 g_win_w = 1280, g_win_h = 720;

void android_native_update_mode(void){
  /* В доке 1080p только по docked_1080p: Tegra X1 через nouveau тянет 3D-шутер
   * в 720p заметно легче, а растянет картинку на ТВ сам компоновщик. */
  const int docked = (appletGetOperationMode() == AppletOperationMode_Console);
  if (docked && config.docked_1080p) { g_win_w = 1920; g_win_h = 1080; }
  else                               { g_win_w = 1280; g_win_h = 720;  }

  if (config.portrait) {                 /* engine sees the panel turned 90 deg */
    g_w = g_win_h; g_h = g_win_w;
  } else {                               /* landscape: the two agree            */
    g_w = g_win_w; g_h = g_win_h;
  }

  /* Keep the globals imports.c falls back on in step with the real render size.
   * They matter because eglQuerySurface reports 0x0 on this platform (a known
   * mesa quirk the swap path already logs), so these ARE the numbers the engine
   * gets when it asks how big its surface is -- and they change on docking. */
  screen_width  = (int)g_w;
  screen_height = (int)g_h;
}
/* Reported dpi. Scaled with the render resolution so the physical size the game
 * derives from it stays constant: handheld renders 720x1280 at 237 dpi = 6.2"
 * diagonal, docked renders 1080x1920 and needs 1.5x the dpi to describe the same
 * 6.2" device. Without this the game would compute a tablet when docked and
 * could change layout mid-session. */
int nx_screen_dpi(void) {
  const int base = SCREEN_DPI_HANDHELD;
  if (g_win_w >= 1920) return (base * 3 + 1) / 2;   /* 1.5x, rounded */
  return base;
}

u32 android_native_window_width (void){ return g_win_w; }
u32 android_native_window_height(void){ return g_win_h; }
/* One-shot report of exactly what the game is told about the display. Papers,
 * Please chooses its layout from these (HostUnity.Monitor.detectPlatformKind
 * reads dpi, physical size and aspect), so if the presentation looks wrong these
 * four numbers are the first thing to check against the panel. */
void android_native_log_metrics(void) {
  static int done = 0;
  if (done) return;
  done = 1;
  const float dpi = (float)nx_screen_dpi();
  debugPrintf("[gfx] reported to game: %ux%u @ %d dpi = %.2f\" x %.2f\" "
              "(%.2f\" diag), aspect %.3f, panel %ux%u, portrait=%d\n",
              g_w, g_h, nx_screen_dpi(),
              (double)(g_w / dpi), (double)(g_h / dpi),
              (double)(sqrtf((float)g_w * g_w + (float)g_h * g_h) / dpi),
              (double)((float)g_w / (float)g_h),
              (appletGetOperationMode() == AppletOperationMode_Console) ? 1920u : 1280u,
              (appletGetOperationMode() == AppletOperationMode_Console) ? 1080u : 720u,
              config.portrait);
}

u32 android_native_width(void)  { return g_w; }
u32 android_native_height(void) { return g_h; }

/* ==========================================================================
 * ANativeWindow  ->  libnx NWindow
 * ========================================================================== */
/* fbstub45: pin the displayed region to exactly the dimensions Unity renders
 * into. nwindowSetDimensions may allocate a width-aligned (e.g. 720 -> 768)
 * swapchain buffer; without a matching crop the compositor can scan the extra
 * uninitialized columns, which shows up as the image being "cut off" / garbage
 * on the right edge. Cropping to (0,0,bw,bh) guarantees only the rendered
 * content is presented (stretched to the panel, no cutoff). */
/* TATE-mode rotation, ported from the VLN reference port: the portrait render
 * is rotated by the COMPOSITOR onto the landscape panel via the BufferQueue
 * per-buffer transform flag (nwindowSetTransform -> bqinput.transform on every
 * queue). Producer-side only -- needs none of the vi:m layer permissions that
 * blocked the pillarbox route (rc=0xdc01, handoff §6.2). Correct aspect, zero
 * GPU cost; the player holds the console rotated. config.portrait: 1 = ROT_90
 * CW (default, right Joy-Con up), 2 = ROT_270 CCW (left Joy-Con up),
 * 0 = none (previous stretched-16:9 behavior). Touch + stick cursor are
 * remapped through the same rotation in android_native_feed_hid. */
#ifndef NATIVE_WINDOW_TRANSFORM_ROT_90
#define NATIVE_WINDOW_TRANSFORM_ROT_90  0x4   /* HAL_TRANSFORM_ROT_90  */
#endif
#ifndef NATIVE_WINDOW_TRANSFORM_ROT_270
#define NATIVE_WINDOW_TRANSFORM_ROT_270 0x7   /* ROT_90 | ROT_180      */
#endif
/* Unused since pp_tate.c took over rotation: asking the compositor to rotate as
 * well would rotate twice. Kept because it documents the transform constants and
 * is the one-line path back to compositor rotation if the GL route is ever
 * dropped. */
__attribute__((unused))
static u32 nx_portrait_transform(void) {
  if (config.portrait == 2) return (u32)NATIVE_WINDOW_TRANSFORM_ROT_270;
  if (config.portrait == 1) return (u32)NATIVE_WINDOW_TRANSFORM_ROT_90;
  return 0;
}

/* Set dimensions, then crop to what the window ACTUALLY became.
 *
 * The base cropped to the REQUESTED size and only logged the readback. That is
 * the black-screen bug: if the real window is not the size we asked for, the
 * crop rectangle falls outside it and nvnflinger rejects every single present
 * with BAD_VALUE (0x95d = "2349-0004"), so nothing ever reaches the display.
 * Observed exactly that: geometry set to 720x1280 at gfx init, but by the first
 * swap the window read back as 1280x720 while the crop still said (0,0,720,1280)
 * -- a crop 560 pixels taller than the window it applies to.
 *
 * A crop can never legally exceed the window, so clamp to the readback. If the
 * two disagree we also say so loudly, because that mismatch means something
 * downstream (mesa's EGL surface creation is the prime suspect) is overriding
 * the size we asked for, and that is worth knowing about directly rather than
 * inferring from a black screen. */
static void nx_window_set_geom(NWindow *w, u32 bw, u32 bh) {
  Result rc = nwindowSetDimensions(w, bw, bh);
  /* No compositor transform: pp_tate.c performs the rotation in GL. Asking the
   * compositor to rotate as well would rotate twice. */
  nwindowSetTransform(w, 0);

  u32 aw = 0, ah = 0;
  if (R_FAILED(nwindowGetDimensions(w, &aw, &ah)) || aw == 0 || ah == 0) {
    aw = bw; ah = bh;                      /* readback unavailable: trust our request */
  }

  /* Crop to the SMALLER of what we asked for and what we got. Both bounds are
   * real, and each guards against a different failure:
   *
   *   <= readback  -- a crop may never exceed the window. Exceeding it is what
   *                   made nvnflinger reject every present with BAD_VALUE
   *                   (0x95d) and produced the black screen.
   *
   *   <= requested -- nwindowSetDimensions may allocate a WIDTH-ALIGNED buffer
   *                   (720 rounds up to 768). Those extra columns are never
   *                   rendered into, so presenting them stretches the image
   *                   horizontally and scans uninitialised memory down one edge.
   *
   * The base cropped to the requested size only, which is correct for alignment
   * but not for a window that came back smaller; cropping to the readback only
   * (what this port did briefly) is the reverse mistake. min() is right for
   * both, and equals the old behaviour whenever the sizes already agree. */
  const u32 cw = (aw < bw) ? aw : bw;
  const u32 ch = (ah < bh) ? ah : bh;
  nwindowSetCrop(w, 0, 0, (s32)cw, (s32)ch);

  debugPrintf("[gfx] window geom: requested %ux%u (rc=0x%x), nwindow reports %ux%u, "
              "crop 0,0,%u,%u, portrait=%d (1=ROT90 2=ROT270 0=none)\n",
              bw, bh, rc, aw, ah, cw, ch, config.portrait);
  if (aw != bw || ah != bh)
    debugPrintf("[gfx] *** WINDOW SIZE MISMATCH: asked %ux%u, got %ux%u. Cropping to the\n"
                "[gfx] *** real size so presents are not rejected, but the engine is\n"
                "[gfx] *** rendering %ux%u -- the image will be wrong until this agrees.\n"
                "[gfx] *** Try `portrait 0` in config.txt (window == panel).\n",
                bw, bh, aw, ah, bw, bh);
}

/* Re-assert geometry ONLY on a real dock/undock transition.
 *
 * This used to run every frame, and it made things worse rather than better.
 * Once mesa has created the EGL window surface, nwindowSetDimensions FAILS
 * (observed rc=0xf59) and the window stays at the panel size -- so a per-frame
 * re-assert could not fix the size, but it DID recompute the crop from the
 * failed readback:
 *
 *     requested 720x1280, nwindow reports 1280x720  ->  crop 0,0,720,720
 *
 * That crops a 1280-tall render down to its top 720 rows, which the compositor
 * then stretches over the full panel -- a pure vertical squash, with the width
 * left correct. Exactly the reported symptom, and entirely self-inflicted.
 *
 * The geometry set before surface creation is the one that counts, and it
 * succeeds. After that, leave the window alone; only a dock transition (which
 * really does change the panel) justifies touching it again. */
void android_native_reassert_geom(void) {
  static int last_mode = -1;
  const int mode = (int)appletGetOperationMode();
  if (mode == last_mode) return;            /* no transition -> nothing to do */
  last_mode = mode;

  NWindow *w = nwindowGetDefault();
  u32 aw = 0, ah = 0;
  if (R_SUCCEEDED(nwindowGetDimensions(w, &aw, &ah)) && aw == g_win_w && ah == g_win_h)
    return;                                 /* already correct */
  debugPrintf("[gfx] dock transition (mode=%d): re-asserting %ux%u\n", mode, g_win_w, g_win_h);
  nx_window_set_geom(w, g_win_w, g_win_h);
}

ANativeWindow *android_native_window(void){
  NWindow *w = nwindowGetDefault();
  android_native_update_mode();
  nx_window_set_geom(w, g_win_w, g_win_h);   /* the REAL window is the panel */
  return (ANativeWindow *)w;
}
void     ANativeWindow_acquire(ANativeWindow *w){ (void)w; }                 /* singleton: refcount no-op */
void     ANativeWindow_release(ANativeWindow *w){ (void)w; }
ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface){
  (void)env; (void)surface; return android_native_window();               /* one surface == our window */
}
int32_t  ANativeWindow_getWidth (ANativeWindow *w){ (void)w; return (int32_t)g_w; }
int32_t  ANativeWindow_getHeight(ANativeWindow *w){ (void)w; return (int32_t)g_h; }
int32_t  ANativeWindow_getFormat(ANativeWindow *w){ (void)w; return AWINDOW_FORMAT_RGBA_8888; }
int32_t  ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format){
  (void)format;
  /* The NX window is a FIXED-SIZE display. Resizing the real window to a
   * non-native size (e.g. the game's saved 640x1137 low-res, applied EARLY at
   * startup when Unity knows it from PlayerPrefs) makes mesa build a 640x1137
   * swapchain whose buffers the NX display path never consumes -> the first
   * eglSwapBuffers blocks forever (the boot-2 hang). Android devices without
   * hardware resolution scaling behave exactly like this fix: the resize
   * "succeeds" (returns 0) but readback (getWidth/getHeight, eglQuerySurface)
   * still shows the native size, which is precisely how Unity detects
   * "Hardware resolution scaling not supported" and falls back to its software
   * blit -- the same path that already works when SetResolution happens late
   * at frame 2. So: accept only the native geometry; report success for the
   * rest so Unity's own fallback engages. */
  if (width > 0 && height > 0) {
    g_input_w = width; g_input_h = height;   /* Unity's believed screen == its input space */
    if ((u32)width != g_w || (u32)height != g_h) {
      debugPrintf("[gfx] setBuffersGeometry %dx%d REJECTED (fixed-size window stays %ux%u; engine will blit-scale)\n",
                  width, height, g_w, g_h);
      return 0;
    }
    nx_window_set_geom((NWindow *)w, (u32)width, (u32)height);
  }
  return 0;
}

/* ==========================================================================
 * ALooper -- Unity uses it as a per-thread wait/wake primitive (not real fd
 * polling), so a condvar-backed looper is sufficient. If the engine turns out
 * to register real fds, port cr3_nx's fake-fd PollItem layer in here.
 * ========================================================================== */
#define ALOOPER_POLL_WAKE     (-1)
#define ALOOPER_POLL_TIMEOUT  (-3)
#define MAX_LOOPERS 16

struct ALooper { Mutex m; CondVar cv; int signaled; int refs; u32 owner; int used; };
static struct ALooper g_loopers[MAX_LOOPERS];
static Mutex g_loopers_lock;
static int   g_loopers_init = 0;

static void loopers_once(void){ if(!g_loopers_init){ mutexInit(&g_loopers_lock); g_loopers_init=1; } }

static struct ALooper *looper_for(u32 tid, int create){
  loopers_once();
  mutexLock(&g_loopers_lock);
  for (int i=0;i<MAX_LOOPERS;i++) if (g_loopers[i].used && g_loopers[i].owner==tid){
    struct ALooper *l=&g_loopers[i]; mutexUnlock(&g_loopers_lock); return l; }
  if (create) for (int i=0;i<MAX_LOOPERS;i++) if (!g_loopers[i].used){
    struct ALooper *l=&g_loopers[i];
    l->used=1; l->owner=tid; l->signaled=0; l->refs=1;
    mutexInit(&l->m); condvarInit(&l->cv);
    mutexUnlock(&g_loopers_lock); return l; }
  mutexUnlock(&g_loopers_lock);
  return NULL;
}
static u32 cur_tid(void){ return (u32)(uintptr_t)threadGetCurHandle(); }

ALooper *ALooper_prepare(int opts){ (void)opts; return (ALooper *)looper_for(cur_tid(), 1); }
ALooper *ALooper_forThread(void){  return (ALooper *)looper_for(cur_tid(), 0); }
void     ALooper_acquire(ALooper *l){ struct ALooper *L=(void*)l; if(L){ mutexLock(&L->m); L->refs++; mutexUnlock(&L->m);} }
void     ALooper_release(ALooper *l){ struct ALooper *L=(void*)l; if(L){ mutexLock(&L->m); if(--L->refs<=0) L->used=0; mutexUnlock(&L->m);} }

void ALooper_wake(ALooper *l){
  struct ALooper *L=(void*)l; if(!L) return;
  mutexLock(&L->m); L->signaled=1; condvarWakeAll(&L->cv); mutexUnlock(&L->m);
}
int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData){
  struct ALooper *L = (void*)looper_for(cur_tid(), 1);
  if (outFd) *outFd=0;
  if (outEvents) *outEvents=0;
  if (outData) *outData=NULL;
  mutexLock(&L->m);
  if (!L->signaled){
    if (timeoutMillis==0){ mutexUnlock(&L->m); return ALOOPER_POLL_TIMEOUT; }
    if (timeoutMillis<0)  condvarWait(&L->cv,&L->m);
    else condvarWaitTimeout(&L->cv,&L->m,(u64)timeoutMillis*1000000ull);
  }
  int was = L->signaled; L->signaled=0;
  mutexUnlock(&L->m);
  return was ? ALOOPER_POLL_WAKE : ALOOPER_POLL_TIMEOUT;
}
/* Unity rarely uses these two, but provide them for completeness. */
int ALooper_addFd(ALooper *l,int fd,int ident,int events,void *cb,void *data){
  (void)l;(void)fd;(void)ident;(void)events;(void)cb;(void)data; return 1; }
int ALooper_removeFd(ALooper *l,int fd){ (void)l;(void)fd; return 1; }

/* ==========================================================================
 * Sensors -- report none. (CR3 imported no ASensorManager; Unity does, so these
 * must exist and return a clean empty state rather than be missing symbols.)
 * ========================================================================== */
void *ASensorManager_getInstance(void){ static int x; return &x; }
void *ASensorManager_getInstanceForPackage(const char *p){ (void)p; return ASensorManager_getInstance(); }
int   ASensorManager_getSensorList(void *m, void **list){ (void)m; if(list)*list=NULL; return 0; }
void *ASensorManager_getDefaultSensor(void *m, int type){ (void)m;(void)type; return NULL; }
void *ASensorManager_createEventQueue(void *m, void *looper, int ident, void *cb, void *data){
  (void)m;(void)looper;(void)ident;(void)cb;(void)data; static int q; return &q; }
int   ASensorManager_destroyEventQueue(void *m, void *q){ (void)m;(void)q; return 0; }

int   ASensorEventQueue_enableSensor (void *q, const void *s){ (void)q;(void)s; return -1; }
int   ASensorEventQueue_disableSensor(void *q, const void *s){ (void)q;(void)s; return 0; }
int   ASensorEventQueue_setEventRate (void *q, const void *s, int32_t us){ (void)q;(void)s;(void)us; return 0; }
int   ASensorEventQueue_getEvents    (void *q, void *ev, size_t n){ (void)q;(void)ev;(void)n; return 0; }
int   ASensorEventQueue_hasEvents    (void *q){ (void)q; return 0; }

const char *ASensor_getName      (const void *s){ (void)s; return ""; }
const char *ASensor_getVendor    (const void *s){ (void)s; return ""; }
int         ASensor_getType      (const void *s){ (void)s; return 0; }
float       ASensor_getResolution(const void *s){ (void)s; return 0.0f; }
int         ASensor_getMinDelay  (const void *s){ (void)s; return 0; }

/* cr3 dead-handler stub: no orientation sensor -> report level. */
void android_get_orientation(float *x, float *y, float *z){
  if (x) *x = 0.0f;
  if (y) *y = 0.0f;
  if (z) *z = 0.0f;
}

/* ==========================================================================
 * HID polling -> Unity input.
 * Unity ingests input through the Java UnityPlayer (touch -> nativeInjectEvent /
 * key path). The exact native event struct is engine-internal: recover the
 * registered "injectEvent"/"nativePointer*" method from libunity's JNI_OnLoad
 * (PORTING_PLAN.md S4) and fill in feed_one_touch(). Until then this reads HID
 * but doesn't yet hand it to the engine.
 * ========================================================================== */
/* ==========================================================================
 * HID -> Unity input via nativeInjectEvent.
 * Handheld: touchscreen passes through. Docked (no touch): left stick drives a
 * virtual cursor, A = tap. Builds a fake MotionEvent (unity_input.c) and calls
 * the recovered nativeInjectEvent(env, thiz, event, deviceId).
 * ========================================================================== */
#include "unity_input.h"
#include <stdio.h>
#include "config.h"

extern FILE *fopen_fake(const char *path, const char *mode);
extern int   fclose_fake(FILE *f);

static HidTouchScreenState g_touch;
static int   g_prev_touch = 0;        /* pointers down last frame */
static float g_last_tx = 360, g_last_ty = 640;  /* last handheld touch (game space) for UP */

void android_native_input_init(void){
  hidInitializeTouchScreen();   /* контроллеры читает nx_bridge.c */
  /* input_log_fn intentionally left unset: the MotionEvent-getter trace is a
   * debug aid (set it to debugPrintf to re-enable). Leaving it off keeps the
   * touch path log-silent so taps don't stutter on slow SD writes. */
}

/* inject signature == recovered nativeInjectEvent: (env,thiz,InputEvent,int)->Z */
typedef uint8_t (*inject_fn)(void*,void*,void*,int);

void android_native_feed_hid(inject_fn inject, void *env, void *thiz){
  /* Кнопки и стики сюда не идут: их забирает C#-слой через nx_bridge и отдаёт
   * игре виртуальным геймпадом Input System. Здесь только сенсорный экран. */

  /* ---- handheld touchscreen ---- */
  int n = hidGetTouchScreenStates(&g_touch, 1);
  if (n > 0 && g_touch.count > 0){
    int   ids[UI_MAX_POINTERS]; float xs[UI_MAX_POINTERS]; float ys[UI_MAX_POINTERS];
    int c = g_touch.count > UI_MAX_POINTERS ? UI_MAX_POINTERS : g_touch.count;
    /* The Switch panel reports touches in its native 1280x720 landscape space;
     * the render is rotated onto it per config.portrait (see nx_window_set_geom).
     * Undo that same rotation so a tap lands where the player sees it. */
    const float PANEL_W = 1280.0f, PANEL_H = 720.0f;
    for (int i=0;i<c;i++){ ids[i]=(int)g_touch.touches[i].finger_id;
      float px=(float)g_touch.touches[i].x, py=(float)g_touch.touches[i].y;
      if (config.portrait == 2) {          /* ROT_270 (CCW): game(PANEL_H-py, px)  */
        xs[i]= (PANEL_H-py) * ((float)g_w / PANEL_H);
        ys[i]=  px          * ((float)g_h / PANEL_W);
      } else if (config.portrait == 1) {   /* ROT_90 (CW): game(py, PANEL_W-px)    */
        xs[i]=  py          * ((float)g_w / PANEL_H);
        ys[i]= (PANEL_W-px) * ((float)g_h / PANEL_W);
      } else {                             /* no rotation: stretched full-panel map */
        xs[i]=  px * ((float)g_w / PANEL_W);
        ys[i]=  py * ((float)g_h / PANEL_H);
      } }
    g_last_tx = xs[0]; g_last_ty = ys[0];               /* remember for the UP */
    int action = g_prev_touch ? AMOTION_ACTION_MOVE : AMOTION_ACTION_DOWN;
    inject(env, thiz, unity_motionevent(action, c, ids, xs, ys), 0);
    g_prev_touch = c;
    return;
  }
  if (g_prev_touch){                                  /* released -> UP at last pos */
    int   ids[1]={0}; float xs[1]={g_last_tx}, ys[1]={g_last_ty};
    inject(env, thiz, unity_motionevent(AMOTION_ACTION_UP, 1, ids, xs, ys), 0);
    g_prev_touch = 0;
    return;
  }
}

/* Docked/desktop cursor overlay is now provided by nx_pointer (nxp_draw),
 * called from the eglSwapBuffers wrapper in imports.c. */
