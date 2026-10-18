/* unity_entrypoints.h -- UnityPlayer native-method offsets for PAPERS, PLEASE
 *
 * Recovered from the game's own libunity.so:
 *   Unity 2022.3.62f2 (.note.unity "2022.3.62f2_7670c08855a9")
 *   arm64, IL2CPP, BuildID xxHash e834a80f301c2437, 10,438,952 bytes
 *
 * JNI_OnLoad is exported at libunity+0x49166c and calls ten RegisterNatives
 * helpers; walking each helper's JNINativeMethod[] table yields 42 native
 * methods across 10 classes. The method SET is identical to the Zookeeper DX
 * base, so the loader's main loop binds to it unchanged -- only the offsets
 * differ, because Unity strips unused engine modules out of libunity.so per
 * game and Papers, Please's build is stripped differently.
 *
 * REGENERATE after a game update:
 *     python3 tools/extract_entrypoints.py libunity.so > source/unity_entrypoints.h
 *
 * DO NOT hand-edit. Every value below was emitted by that tool.
 */
#ifndef UNITY_ENTRYPOINTS_H
#define UNITY_ENTRYPOINTS_H

#include <stdint.h>
#include "so_util.h"

#define OFF_JNI_OnLoad 0x49166c

/* RegisterNatives helper #1  fn=0x491708  table@0x9ee0e0  count=26 */
#define OFF_initJni                              0x490868    /* (Landroid/content/Context;)V */
#define OFF_nativeDone                           0x490874    /* ()Z */
#define OFF_nativePause                          0x490904    /* ()Z */
#define OFF_nativeRecreateGfxState               0x490a9c    /* (ILandroid/view/Surface;)V */
#define OFF_nativeSendSurfaceChangedEvent        0x490b04    /* ()V */
#define OFF_nativeRender                         0x490b5c    /* ()Z */
#define OFF_nativeResume                         0x490968    /* ()V */
#define OFF_nativeLowMemory                      0x4909b0    /* ()V */
#define OFF_nativeApplicationUnload              0x4909f8    /* ()V */
#define OFF_nativeFocusChanged                   0x490a48    /* (Z)V */
#define OFF_nativeSetInputArea                   0x490eb0    /* (IIII)V */
#define OFF_nativeSetKeyboardIsVisible           0x490f30    /* (Z)V */
#define OFF_nativeSetInputString                 0x490f88    /* (Ljava/lang/String;)V */
#define OFF_nativeSetInputSelection              0x491028    /* (II)V */
#define OFF_nativeSoftInputClosed                0x491178    /* ()V */
#define OFF_nativeSoftInputCanceled              0x491090    /* ()V */
#define OFF_nativeReportKeyboardConfigChanged    0x491130    /* ()V */
#define OFF_nativeSoftInputLostFocus             0x4910e0    /* ()V */
#define OFF_nativeInjectEvent                    0x490bbc    /* (Landroid/view/InputEvent;I)Z */
#define OFF_nativeUnitySendMessage               0x4911c8    /* (Ljava/lang/String;Ljava/lang/String;[B)V */
#define OFF_nativeIsAutorotationOn               0x491378    /* ()Z */
#define OFF_nativeMuteMasterAudio                0x4913d8    /* (Z)V */
#define OFF_nativeSetLaunchURL                   0x491434    /* (Ljava/lang/String;)V */
#define OFF_nativeOrientationChanged             0x4915b4    /* (II)V */
#define OFF_nativeGetNoWindowMode                0x491614    /* ()Z */
#define OFF_nativeHidePreservedContent           0x49156c    /* ()V */

/* RegisterNatives helper #2  fn=0x4917fc  table@0x9ee350  count=1 */
#define OFF_nOnChoreographer                     0x822198    /* (JJ)V */

/* RegisterNatives helper #3  fn=0x4918f0  table@0x9ee368  count=2 */
#define OFF_nOnRefreshPeriodChanged              0x824498    /* (JJJJ)V */
#define OFF_nSetSupportedRefreshPeriods          0x8242b8    /* (J[J[I)V */

/* RegisterNatives helper #4  fn=0x46aed8  table@0x9edee8  count=3 */
#define OFF_initializeARCore                     0x46d000    /* (Landroid/app/Activity;)V */
#define OFF_pauseARCore                          0x46d064    /* ()V */
#define OFF_resumeARCore                         0x46d0b8    /* ()V */

/* RegisterNatives helper #5  fn=0x48a0b4  table@0x9ee000  count=4 */
#define OFF_initCamera2Jni                       0x48a064    /* ()V */
#define OFF_deinitCamera2Jni                     0x48a0b0    /* ()V */
#define OFF_nativeFrameReady                     0x48e13c    /* (Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;III)V */
#define OFF_nativeSurfaceTextureReady            0x48dfd4    /* (Ljava/lang/Object;)V */

/* RegisterNatives helper #6  fn=0x47033c  table@0x9edf60  count=2 */
#define OFF_initHFPStatusJni                     0x4702ec    /* ()V */
#define OFF_deinitHFPStatusJni                   0x470338    /* ()V */

/* RegisterNatives helper #7  fn=0x476668  table@0x9edfa0  count=1 */
#define OFF_onAudioVolumeChanged                 0x476614    /* (I)V */

/* RegisterNatives helper #8  fn=0x46f718  table@0x9edf48  count=1 */
#define OFF_nativeStatusQueryResult              0x46f4ac    /* (Ljava/lang/String;II)V */

/* RegisterNatives helper #9  fn=0x4768bc  table@0x9edfb8  count=1 */
#define OFF_nativeUpdateOrientationLockState     0x476858    /* (I)V */

/* RegisterNatives helper #10  fn=0x46a1c4  table@0x9eded0  count=1 */
#define OFF_nativeGetSoftInputType               0x46a160    /* ()I */

/* total native methods recovered: 42 */
/* ---- JNI native signatures: ret (*)(JNIEnv*, jobject thiz, args...) -------
 * Every registered native takes the JNIEnv* and the jobject `thiz` first; these
 * cover every shape in the table above. main.c binds the drive-critical ones to
 * these types in nx_resolve_entrypoints(). */
typedef void     (*fn_initJni)(void*,void*,void*);
typedef void     (*fn_gfxstate)(void*,void*,int32_t,void*);
typedef void     (*fn_v)(void*,void*);
typedef uint8_t  (*fn_z)(void*,void*);
typedef void     (*fn_vz)(void*,void*,int32_t);
typedef uint8_t  (*fn_inject)(void*,void*,void*,int32_t);
typedef void     (*fn_orient)(void*,void*,int32_t,int32_t);

/* The .so links at base 0, so a runtime address is just base + offset. */
#define UNITY_RESOLVE(mod, off) ((void*)((uintptr_t)(mod).load_virtbase + (off)))

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
