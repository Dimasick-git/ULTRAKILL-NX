/* unity_jni.h -- real handlers for the Android framework classes Unity needs.
 *
 * jni_fake.c delegates these classes here before its own generic handlers
 * (unity_owns_class() at the top of each dispatch_*); everything else stays a
 * no-op stub there:
 *
 *   android/content/res/AssetManager      -> open()/openFd() real files
 *   java/io/InputStream                   -> read()/available()/close()
 *   android/content/res/AssetFileDescriptor + java/io/FileDescriptor (mmap path)
 *   android/content/SharedPreferences     -> PlayerPrefs read   (save data)
 *   android/content/SharedPreferences$Editor -> PlayerPrefs write
 *   android/view/Display + DisplayManager + Configuration + Resources -> screen
 *   android/content/Context               -> data/files paths
 *   com/unity3d/player/UnityPlayer        -> host queries (mostly no-op)
 */
#ifndef UNITY_JNI_H
#define UNITY_JNI_H

#include <stdarg.h>
#include <stdint.h>

/* FakeID is defined in jni_fake.c; we only read .cls/.name/.sig (all char[]). */

void  unity_jni_init(const char *data_root);
int   unity_owns_class(const char *cls);          /* 1 if this module handles it */

void     *unity_dispatch_object(void *recv, const void *id, va_list va);
uint64_t  unity_dispatch_int   (void *recv, const void *id, va_list va); /* int/bool/long */
void      unity_dispatch_void  (void *recv, const void *id, va_list va);

/* Boxed PlayerPrefs values returned by getAll() iteration. jni_fake.c routes
 * unbox calls (intValue/longValue/booleanValue/floatValue) and IsInstanceOf by
 * RECEIVER so only our own boxed handles are affected (other code's Integer/etc.
 * objects are untouched). */
int       unity_is_boxed   (void *recv);                 /* 1 if recv is our boxed value */
uint64_t  unity_boxed_int  (void *recv);                 /* int/long/bool payload */
float     unity_boxed_float (void *recv);                /* float payload */
int       unity_isinstance (void *obj, const char *clazz); /* 1/0 for boxed, -1 = not ours */

/* provided by jni_fake.c (see integration note 4) */
extern void       *jni_make_string(const char *utf);
extern void       *jni_make_object(const char *label);
extern void       *jni_bytearray_data(void *arr, int *len_out);
extern const char *jni_string_utf(void *jstr);

#endif /* UNITY_JNI_H */
