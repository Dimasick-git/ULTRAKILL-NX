/* unity_glue.c -- ties the port modules together: the fake "environment"
 * objects the engine is driven with, and a single init entry point.
 *
 * The fake JNIEnv (`fake_env`) is cr3_nx's -- jni_fake.c builds the JNIEnv
 * function table and exposes it. The three jobjects below are opaque handles the
 * engine receives as `thiz` / Context / Surface arguments; it never inspects
 * their innards (ANativeWindow_fromSurface ignores the Surface and returns our
 * singleton NWindow), so plain interned handles suffice.
 */

#include "unity_jni.h"          /* unity_jni_init, jni_make_object */

/* engine-facing singletons (declared extern in main.c) */
void *fake_unityplayer_thiz = 0;
void *fake_context_obj      = 0;
void *fake_surface_obj      = 0;

/* Call once at startup, AFTER jni_fake_init() (which creates fake_env and the
 * class/method pools) and BEFORE resolving/driving the engine. */
void unity_environment_init(const char *data_root)
{
  /* asset I/O, PlayerPrefs save data, Display/Configuration handlers */
  unity_jni_init(data_root);

  /* the objects the lifecycle calls pass in. jni_fake interns classes lazily on
   * FindClass, so these handles are all the engine needs. */
  fake_unityplayer_thiz = jni_make_object("com/unity3d/player/UnityPlayer");
  fake_context_obj      = jni_make_object("android/content/Context");
  fake_surface_obj      = jni_make_object("android/view/Surface");
}
