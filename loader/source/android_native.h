/* android_native.h -- NDK NativeActivity types, kept from the base loader
 * (cr3_nx, a NativeActivity host).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * libunity is not a NativeActivity: the engine is driven through its JNI
 * natives, and the NDK functions it does import live in android_native_unity.c
 * and ndk_stubs.c. This header remains for the shared NDK typedefs and the
 * fake-fd helpers. The ABI of ANativeActivity{,Callbacks} and the A* functions
 * must match the NDK exactly.
 */

#ifndef __ANDROID_NATIVE_H__
#define __ANDROID_NATIVE_H__

#include <stdint.h>
#include <stddef.h>

// --- NDK opaque types (we own the concrete instances) -----------------------
typedef struct ANativeWindow  ANativeWindow;   // == libnx NWindow* at runtime
typedef struct AInputQueue    AInputQueue;
typedef struct AInputEvent    AInputEvent;
typedef struct ALooper        ALooper;
typedef struct AConfiguration AConfiguration;
typedef struct AAssetManager  AAssetManager;
typedef struct ANativeActivity ANativeActivity;

typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);

// --- ANativeActivityCallbacks (NDK android/native_activity.h layout) --------
typedef struct ANativeActivityCallbacks {
  void  (*onStart)(ANativeActivity *activity);
  void  (*onResume)(ANativeActivity *activity);
  void *(*onSaveInstanceState)(ANativeActivity *activity, size_t *outSize);
  void  (*onPause)(ANativeActivity *activity);
  void  (*onStop)(ANativeActivity *activity);
  void  (*onDestroy)(ANativeActivity *activity);
  void  (*onWindowFocusChanged)(ANativeActivity *activity, int hasFocus);
  void  (*onNativeWindowCreated)(ANativeActivity *activity, ANativeWindow *window);
  void  (*onNativeWindowResized)(ANativeActivity *activity, ANativeWindow *window);
  void  (*onNativeWindowRedrawNeeded)(ANativeActivity *activity, ANativeWindow *window);
  void  (*onNativeWindowDestroyed)(ANativeActivity *activity, ANativeWindow *window);
  void  (*onInputQueueCreated)(ANativeActivity *activity, AInputQueue *queue);
  void  (*onInputQueueDestroyed)(ANativeActivity *activity, AInputQueue *queue);
  void  (*onContentRectChanged)(ANativeActivity *activity, const void *rect);
  void  (*onConfigurationChanged)(ANativeActivity *activity);
  void  (*onLowMemory)(ANativeActivity *activity);
} ANativeActivityCallbacks;

struct ANativeActivity {
  ANativeActivityCallbacks *callbacks;
  void       *vm;     // JavaVM*
  void       *env;    // JNIEnv*
  void       *clazz;  // the MyNativeActivity jobject
  const char *internalDataPath;
  const char *externalDataPath;
  int32_t     sdkVersion;
  void       *instance;       // glue stashes its android_app* here
  AAssetManager *assetManager;
  const char *obbPath;
};

typedef void ANativeActivity_createFunc(ANativeActivity *activity, void *savedState, size_t savedStateSize);

// --- host control API (used by main.c) --------------------------------------

// initialise the fake-fd / looper layer. Call once before ANativeActivity_onCreate.
void android_native_init(void);

// in-process fake-fd layer backing the glue's command pipe; libc_shim routes
// read/write/close/pipe here for fds in the fake range.
int  fakefd_is_fake(int fd);
int  fakefd_pipe(int fds[2]);
long fakefd_read(int fd, void *buf, unsigned long n);
long fakefd_write(int fd, const void *buf, unsigned long n);
int  fakefd_close(int fd);

// build the ANativeActivity the glue's onCreate consumes. Paths must outlive it.
ANativeActivity *android_make_activity(void *vm, void *env, void *clazz,
                                       AAssetManager *am,
                                       const char *internalPath,
                                       const char *externalPath,
                                       const char *obbPath);

// the single ANativeWindow handed to onNativeWindowCreated (wraps the default
// libnx NWindow). The engine passes it straight to eglCreateWindowSurface.
ANativeWindow *android_native_window(void);

// the AInputQueue handed to onInputQueueCreated; main.c injects HID into it.
AInputQueue *android_input_queue(void);

// --- input injection (called from the main/UI thread) -----------------------
// action: AMOTION_EVENT_ACTION_* ; coords are in window pixels.
void android_inject_motion(int32_t action, int pointer_count,
                           const int32_t *ids, const float *xs, const float *ys);
// action: AKEY_EVENT_ACTION_DOWN/UP ; keycode: AKEYCODE_*
void android_inject_key(int32_t action, int32_t keycode);

// accelerometer feed for MyNativeActivity.getOrientation* / ASensor path
void android_set_orientation(float x, float y, float z);
void android_get_orientation(float *x, float *y, float *z);

// 1 once android_main() has returned (engine asked to quit).
int android_main_finished(void);
void android_mark_main_finished(void);

#endif
