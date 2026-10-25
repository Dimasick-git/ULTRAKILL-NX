/* unity_entrypoints.h -- function types of UnityPlayer's JNI natives.
 *
 * libunity's JNI_OnLoad registers 42 native methods across 10 classes. Their
 * addresses are captured at runtime from RegisterNatives (jni_fake.c), with
 * unity.<name> keys in offsets.ini as the fallback (unity_native() in main.c),
 * so nothing here depends on a particular libunity build.
 */
#ifndef UNITY_ENTRYPOINTS_H
#define UNITY_ENTRYPOINTS_H

#include <stdint.h>
#include "so_util.h"

/* ---- JNI native signatures: ret (*)(JNIEnv*, jobject thiz, args...) -------
 * Every registered native takes the JNIEnv* and the jobject `thiz` first; these
 * cover every shape main.c drives. */
typedef void     (*fn_initJni)(void*,void*,void*);
typedef void     (*fn_gfxstate)(void*,void*,int32_t,void*);
typedef void     (*fn_v)(void*,void*);
typedef uint8_t  (*fn_z)(void*,void*);
typedef void     (*fn_vz)(void*,void*,int32_t);
typedef uint8_t  (*fn_inject)(void*,void*,void*,int32_t);
typedef void     (*fn_orient)(void*,void*,int32_t,int32_t);

/* ===========================================================================
 * Drive sequence -- what the Java UnityPlayer does, and what main.c does here:
 *
 *   initJni(env, thiz, fake_context);                  // early init
 *   nativeRecreateGfxState(env, thiz, 0, fake_surface);// hand it the surface
 *   nativeSendSurfaceChangedEvent(env, thiz);
 *   nativeResume(env, thiz);                           // REQUIRED: the player
 *   nativeFocusChanged(env, thiz, 1);                  // loop only ticks when
 *                                                      // resumed AND focused
 *   per frame:
 *     nativeInjectEvent(env, thiz, motionEvent, 0);    // input
 *     nativeRender(env, thiz);                         // returns 0 to quit
 * ======================================================================== */

#endif /* UNITY_ENTRYPOINTS_H */
