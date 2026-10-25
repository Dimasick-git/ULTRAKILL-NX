/* jni_fake.h -- fake JNI environment (JavaVM / JNIEnv) for the game's Android
 * libraries, so their Java calls resolve to native code. Unity and input
 * classes are handled by unity_jni.c / unity_input.c; the Text2Bitmap /
 * MoviePlayer handlers kept from the base loader are unused (cr3_stubs.c).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

#include <stdint.h>

extern void *fake_vm;  // JavaVM *
extern void *fake_env; // JNIEnv *

// set when the engine asks the activity to finish
extern volatile int jni_quit_requested;

void jni_init(void);

// the fake MyNativeActivity jobject handed to ANativeActivity.clazz
void *jni_make_activity_object(void);

// fake Java object / string constructors
void *jni_make_string(const char *utf);
void *jni_make_object(const char *label);

#endif
