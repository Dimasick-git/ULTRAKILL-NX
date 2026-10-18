/* ndk_stubs.c -- no-op bodies for the NDK functions imports.c registers that
 * cr3_nx's dropped android_native.c / text2bitmap.c used to define.
 *
 * PAPERS, PLEASE (Unity IL2CPP) never calls these: it loads assets through the
 * Java AssetManager (handled in unity_jni.c) and feeds input via JNI
 * nativeInjectEvent (unity_input.c), not the AInputQueue/AMotionEvent NDK path.
 * They exist only so the imports.c table links. Signatures match the extern
 * declarations in imports.c exactly.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nx_gameroot.h"
#include <stddef.h>

/* AInputQueue / AInputEvent / AMotionEvent / AKeyEvent */
void    AInputQueue_attachLooper(void *a, void *b, int c, void *d, void *e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void    AInputQueue_detachLooper(void *a) { (void)a; }
int32_t AInputQueue_getEvent(void *a, void **b) { (void)a;(void)b; return -1; }   /* no events */
int32_t AInputQueue_preDispatchEvent(void *a, void *b) { (void)a;(void)b; return 0; }
void    AInputQueue_finishEvent(void *a, void *b, int c) { (void)a;(void)b;(void)c; }
int32_t AInputEvent_getType(const void *a) { (void)a; return 0; }
int32_t AMotionEvent_getAction(const void *a) { (void)a; return 0; }
size_t  AMotionEvent_getPointerCount(const void *a) { (void)a; return 0; }
int32_t AMotionEvent_getPointerId(const void *a, size_t b) { (void)a;(void)b; return 0; }
float   AMotionEvent_getX(const void *a, size_t b) { (void)a;(void)b; return 0.0f; }
float   AMotionEvent_getY(const void *a, size_t b) { (void)a;(void)b; return 0.0f; }
int32_t AKeyEvent_getKeyCode(const void *a) { (void)a; return 0; }
int32_t AKeyEvent_getFlags(const void *a) { (void)a; return 0; }
int32_t AKeyEvent_getRepeatCount(const void *a) { (void)a; return 0; }

/* AConfiguration -- Unity's native systeminfo::GetSystemLanguage (behind
 * Application.systemLanguage) reads the device language through THIS NDK path on
 * Android, NOT via JNI Locale.getLanguage(). It was hardcoded to "en"/"US",
 * which pinned Application.systemLanguage to English no matter what config.txt
 * said -> the game's Localizer always picked the English assets. Return the
 * resolved language instead. NDK convention: write exactly the 2-char code,
 * not NUL-terminated. */
void *AConfiguration_new(void) {
  /* MUST be non-NULL: libunity's systeminfo::GetSystemLanguage does
   * `cfg = AConfiguration_new(); ...; AConfiguration_getLanguage(cfg, buf)` but
   * skips the getLanguage read (and defaults to English) when cfg is NULL. A
   * static buffer is enough -- the struct is opaque to us and libunity reads the
   * language back through AConfiguration_getLanguage(), not the struct. */
  static char cfg[256];
  extern int debugPrintf(char *, ...);
  debugPrintf("[lang] AConfiguration_new() -> non-NULL\n");
  return cfg;
}
void  AConfiguration_fromAssetManager(void *a, void *b) { (void)a;(void)b; }
void  AConfiguration_getLanguage(void *a, char *out) {
  (void)a;
  if (!out) return;
  extern const char *nx_resolved_language(void);   /* "ja" or "en" (jni_fake.c) */
  extern int debugPrintf(char *, ...);
  const char *lc = nx_resolved_language();
  out[0] = lc[0]; out[1] = lc[1];
  debugPrintf("[lang] AConfiguration_getLanguage -> %c%c (drives Application.systemLanguage)\n",
              out[0], out[1]);
}
void  AConfiguration_getCountry(void *a, char *out) {
  (void)a;
  if (!out) return;
  extern const char *nx_resolved_language(void);
  int ja = nx_resolved_language()[0] == 'j';
  out[0] = ja ? 'J' : 'U'; out[1] = ja ? 'P' : 'S';
}
void  AConfiguration_delete(void *a) { (void)a; }

/* AAsset -- backed by real files under <gameroot>/assets/.
 *
 * The base returned NULL here, on the reasoning that Unity reads through the
 * Java AssetManager instead. That held for the game it was written for, whose
 * data all lived under assets/bin/Data and arrived through ordinary file I/O.
 *
 * Papers, Please also ships assets/loc zip files -- sixteen localisation archives
 * that live in StreamingAssets, not in bin/Data. Whether the engine reaches
 * those through the native AAsset API or through UnityWebRequest depends on the
 * path Unity picks at runtime, and a NULL here would be a silent "no
 * localisation" (or worse, a null-deref) rather than an honest error. Serving
 * them from disk costs a few lines and removes the question. If the engine
 * never calls this, nothing is lost.
 *
 * Names arrive relative to the APK's assets/ root, e.g. "loc/en.zip". */
void *AAssetManager_fromJava(void *a, void *b) { (void)a;(void)b;
  /* Non-NULL so callers proceed to AAssetManager_open; the value is opaque and
   * we never dereference it. */
  static int mgr_token;
  return &mgr_token;
}

typedef struct { unsigned char *buf; int64_t len; } NxAsset;

void *AAssetManager_open(void *a, const char *name, int mode) {
  (void)a; (void)mode;
  extern int debugPrintf(char *, ...);
  if (!name || !*name) return NULL;

  char path[512];
  snprintf(path, sizeof path, "%s/assets/%s", game_root(), name);

  FILE *f = fopen(path, "rb");
  if (!f) {
    debugPrintf("[io] AAssetManager_open(%s) -> not found at %s\n", name, path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return NULL; }

  NxAsset *as = (NxAsset *)malloc(sizeof *as);
  if (!as) { fclose(f); return NULL; }
  as->buf = (unsigned char *)malloc((size_t)sz);
  if (!as->buf) { free(as); fclose(f); return NULL; }
  size_t got = fread(as->buf, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) { free(as->buf); free(as); return NULL; }
  as->len = (int64_t)sz;
  debugPrintf("[io] AAssetManager_open(%s) -> %ld bytes\n", name, sz);
  return as;
}
const void *AAsset_getBuffer(void *a) { NxAsset *as=(NxAsset*)a; return as ? as->buf : NULL; }
int64_t AAsset_getLength(void *a)     { NxAsset *as=(NxAsset*)a; return as ? as->len : 0; }
void  AAsset_close(void *a)           { NxAsset *as=(NxAsset*)a; if (as) { free(as->buf); free(as); } }

/* AndroidBitmap (engine dynamic-text path; unused) */
int AndroidBitmap_getInfo(void *a, void *b, void *c) { (void)a;(void)b;(void)c; return -1; }
int AndroidBitmap_lockPixels(void *a, void *b, void **c) { (void)a;(void)b;(void)c; return -1; }
int AndroidBitmap_unlockPixels(void *a, void *b) { (void)a;(void)b; return 0; }
