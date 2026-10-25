/* Android-only codecs are unavailable on Horizon. Report that through the NDK
 * ABI instead of leaving callable poison addresses. Playback uses NxVideo.
 * Signatures/status values follow the Android NDK headers shipped with Unity. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include "so_util.h"
#include "util.h"

#define UNSUPPORTED (-10002)
static int unavailable(void) {
  static unsigned reported;
  if (!__atomic_exchange_n(&reported,1,__ATOMIC_RELAXED))
    debugPrintf("[capability] Android MediaCodec/ImageReader unavailable; NX software video backend required\n");
  return UNSUPPORTED;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void *codec_new(const char *mime){unavailable();return NULL;}
static int media_status(void *object){return unavailable();}
static int codec_configure(void *codec,const void *format,void *surface,void *crypto,uint32_t flags){return unavailable();}
static int codec_surface(void *codec,void *surface){return unavailable();}
static uint8_t *codec_buffer(void *codec,size_t index,size_t *size){if(size)*size=0;unavailable();return NULL;}
static ssize_t codec_input(void *codec,int64_t timeout){return unavailable();}
/* NDK AMediaCodecBufferInfo: offset, size, presentationTimeUs, flags + padding. */
typedef struct {int32_t offset,size;int64_t timestamp;uint32_t flags;} CodecInfo;
static ssize_t codec_output(void *codec,CodecInfo *info,int64_t timeout){if(info)memset(info,0,sizeof *info);return unavailable();}
static int codec_queue(void *codec,size_t index,int64_t offset,size_t size,uint64_t time,uint32_t flags){return unavailable();}
static int codec_release(void *codec,size_t index,bool render){return unavailable();}
static void *codec_format(void *codec){unavailable();return NULL;}
static void *source_new(void){unavailable();return NULL;}
static void object_delete(void *object){}
static void source_userdata(void *source,void *userdata){}
static void source_read(void *source,ssize_t (*callback)(void*,int64_t,void*,size_t)){}
static void source_size(void *source,int64_t (*callback)(void*)){}
static void source_close(void *source,void (*callback)(void*)){}
static int extractor_fd(void *extractor,int fd,int64_t offset,int64_t size){return unavailable();}
static int extractor_url(void *extractor,const char *url){return unavailable();}
static int extractor_source(void *extractor,void *source){return unavailable();}
static size_t extractor_count(void *extractor){unavailable();return 0;}
static void *extractor_format(void *extractor,size_t index){unavailable();return NULL;}
static int extractor_select(void *extractor,size_t index){return unavailable();}
static ssize_t extractor_read(void *extractor,uint8_t *buffer,size_t capacity){unavailable();return -1;}
static int extractor_index(void *extractor){unavailable();return -1;}
static int64_t extractor_time(void *extractor){unavailable();return -1;}
static bool extractor_advance(void *extractor){unavailable();return false;}
static int extractor_seek(void *extractor,int64_t time,int mode){return unavailable();}
static bool format_i32(void *format,const char *name,int32_t *out){if(out)*out=0;unavailable();return false;}
static bool format_i64(void *format,const char *name,int64_t *out){if(out)*out=0;unavailable();return false;}
static bool format_float(void *format,const char *name,float *out){if(out)*out=0;unavailable();return false;}
static bool format_string(void *format,const char *name,const char **out){if(out)*out=NULL;unavailable();return false;}
static void format_set(void *format,const char *name,int32_t value){unavailable();}
static int image_out(void *object,void **out){if(out)*out=NULL;return unavailable();}
static int image_time(const void *image,int64_t *out){if(out)*out=0;return unavailable();}
static void image_delete_async(void *image,int fence){}
static int reader_new(int32_t width,int32_t height,int32_t format,uint64_t usage,int32_t count,void **out){if(out)*out=NULL;return unavailable();}
static int reader_listener(void *reader,void *listener){return unavailable();}
static void hardware_acquire(void *buffer){unavailable();}
static void hardware_release(void *buffer){}
typedef struct {uint32_t width,height,layers,format;uint64_t usage;uint32_t stride,reserved0;uint64_t reserved1;} HardwareDesc;
static void hardware_describe(const void *buffer,HardwareDesc *out){if(out)memset(out,0,sizeof *out);unavailable();}
static void *native_surface(void *env,void *window){unavailable();return NULL;}
static int no_execl(const char *path,const char *arg,...){errno=ENOSYS;return -1;}
#pragma GCC diagnostic pop

/* These are real missing libc operations, not unsupported media APIs. */
extern int __android_log_write(int,const char*,const char*);
static int log_buf_write(int buffer,int priority,const char *tag,const char *text){(void)buffer;return __android_log_write(priority,tag,text);}
extern int ALooper_pollOnce(int,int*,int*,void**);
static char *case_find(const char *text,const char *needle){
  if (!text || !needle) return NULL;
  size_t n=strlen(needle);if(!n)return (char*)text;
  for (;*text;++text) if(!strncasecmp(text,needle,n))return (char*)text;
  return NULL;
}
typedef struct {void *base;size_t size;} CompatIovec;
extern void nx_fd_lock(int),nx_fd_unlock(int);
extern int fakefd_is_fake(int);
extern long fakefd_write(int,const void*,size_t);
static ssize_t vector_write(int fd,const CompatIovec *vectors,int count){
  if(count<0 || count>1024 || (count && !vectors)){errno=EINVAL;return -1;}
  size_t wanted=0;
  for(int i=0;i<count;++i){
    if(vectors[i].size && !vectors[i].base){errno=EFAULT;return -1;}
    if(vectors[i].size>(SIZE_MAX>>1)-wanted){errno=EINVAL;return -1;}
    wanted+=vectors[i].size;
  }
  int fake=fakefd_is_fake(fd);ssize_t total=0;
  if(!fake)nx_fd_lock(fd);
  for(int i=0;i<count;++i){
    if(!vectors[i].size)continue;
    ssize_t n=fake?fakefd_write(fd,vectors[i].base,vectors[i].size):write(fd,vectors[i].base,vectors[i].size);
    if(n<0){if(!total)total=-1;break;}
    total+=n;if((size_t)n!=vectors[i].size)break;
  }
  if(!fake)nx_fd_unlock(fd);
  return total;
}

/* Imported DATA symbols must point at a pointer variable, not string bytes. */
#define KEY(name,value) static const char *key_##name=value
KEY(CHANNEL_COUNT,"channel-count");KEY(COLOR_FORMAT,"color-format");KEY(COLOR_RANGE,"color-range");
KEY(COLOR_STANDARD,"color-standard");KEY(DURATION,"durationUs");KEY(ENCODER_DELAY,"encoder-delay");
KEY(FRAME_RATE,"frame-rate");KEY(HEIGHT,"height");KEY(LANGUAGE,"language");KEY(MIME,"mime");
KEY(ROTATION,"rotation-degrees");KEY(SAMPLE_RATE,"sample-rate");KEY(SLICE_HEIGHT,"slice-height");KEY(STRIDE,"stride");KEY(WIDTH,"width");
#define FN(name,target) {name,(uintptr_t)&target}
#define DATA(name) FN("AMEDIAFORMAT_KEY_" #name,key_##name)
DynLibFunction nx_android_compat_functions[]={
 FN("nearbyintf",nearbyintf),FN("strcasestr",case_find),FN("execl",no_execl),FN("writev",vector_write),
 FN("__android_log_buf_write",log_buf_write),FN("ALooper_pollAll",ALooper_pollOnce),FN("ANativeWindow_toSurface",native_surface),
 DATA(CHANNEL_COUNT),DATA(COLOR_FORMAT),DATA(COLOR_RANGE),DATA(COLOR_STANDARD),DATA(DURATION),DATA(ENCODER_DELAY),
 DATA(FRAME_RATE),DATA(HEIGHT),DATA(LANGUAGE),DATA(MIME),DATA(ROTATION),DATA(SAMPLE_RATE),DATA(SLICE_HEIGHT),DATA(STRIDE),DATA(WIDTH),
 FN("AMediaCodec_createDecoderByType",codec_new),FN("AMediaCodec_delete",media_status),FN("AMediaCodec_configure",codec_configure),
 FN("AMediaCodec_start",media_status),FN("AMediaCodec_stop",media_status),FN("AMediaCodec_flush",media_status),
 FN("AMediaCodec_getInputBuffer",codec_buffer),FN("AMediaCodec_getOutputBuffer",codec_buffer),FN("AMediaCodec_dequeueInputBuffer",codec_input),
 FN("AMediaCodec_dequeueOutputBuffer",codec_output),FN("AMediaCodec_queueInputBuffer",codec_queue),FN("AMediaCodec_releaseOutputBuffer",codec_release),
 FN("AMediaCodec_getOutputFormat",codec_format),FN("AMediaCodec_setOutputSurface",codec_surface),
 FN("AMediaDataSource_new",source_new),FN("AMediaDataSource_delete",object_delete),FN("AMediaDataSource_setUserdata",source_userdata),
 FN("AMediaDataSource_setReadAt",source_read),FN("AMediaDataSource_setGetSize",source_size),FN("AMediaDataSource_setClose",source_close),
 FN("AMediaExtractor_delete",media_status),FN("AMediaExtractor_setDataSourceFd",extractor_fd),FN("AMediaExtractor_setDataSource",extractor_url),
 FN("AMediaExtractor_setDataSourceCustom",extractor_source),FN("AMediaExtractor_getTrackCount",extractor_count),FN("AMediaExtractor_getTrackFormat",extractor_format),
 FN("AMediaExtractor_selectTrack",extractor_select),FN("AMediaExtractor_readSampleData",extractor_read),FN("AMediaExtractor_getSampleTrackIndex",extractor_index),
 FN("AMediaExtractor_getSampleTime",extractor_time),FN("AMediaExtractor_advance",extractor_advance),FN("AMediaExtractor_seekTo",extractor_seek),
 FN("AMediaFormat_delete",media_status),FN("AMediaFormat_getInt32",format_i32),FN("AMediaFormat_getInt64",format_i64),FN("AMediaFormat_getFloat",format_float),
 FN("AMediaFormat_getString",format_string),FN("AMediaFormat_setInt32",format_set),
 FN("AImage_delete",object_delete),FN("AImage_deleteAsync",image_delete_async),FN("AImage_getTimestamp",image_time),FN("AImage_getHardwareBuffer",image_out),
 FN("AImageReader_delete",object_delete),FN("AImageReader_getWindow",image_out),FN("AImageReader_acquireLatestImage",image_out),
 FN("AImageReader_newWithUsage",reader_new),FN("AImageReader_setBufferRemovedListener",reader_listener),FN("AImageReader_setImageListener",reader_listener),
 FN("AHardwareBuffer_acquire",hardware_acquire),FN("AHardwareBuffer_release",hardware_release),FN("AHardwareBuffer_describe",hardware_describe)
};
int nx_android_compat_numfunctions=sizeof(nx_android_compat_functions)/sizeof(*nx_android_compat_functions);
