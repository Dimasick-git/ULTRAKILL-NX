/* Keep ordinary SD logging off UnityMain. Failure to create the worker keeps
 * the previous synchronous behavior. Critical diagnostics explicitly drain. */
#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "nx_log_buffer.h"
#include "nx_gameroot.h"
#include "config.h"

#if DEBUG_LOG
static Mutex queue_lock,writer_lock;
static NxLogBuffer queue;
static char line[16*1024],batch[NX_LOG_CAPACITY];
static FILE *output;
static int initialized,asynchronous;
static uint64_t fallback_flush;
static Thread writer;
static unsigned char stack[0x8000] __attribute__((aligned(0x1000)));
/* The GC collector must never wait for a log mutex owned by a suspended
 * mutator. Its bounded messages are merged only after the world resumes. */
static __thread int collecting;
static NxLogBuffer gc_queue;
static char gc_line[16*1024];
void nx_log_gc_enter(void) { collecting=1; }
void nx_log_gc_leave(void) {
  collecting=0;
  mutexLock(&queue_lock);
  if(gc_queue.size)nx_log_push(&queue,gc_queue.bytes,gc_queue.size);
  queue.dropped+=gc_queue.dropped;
  gc_queue.size=0;gc_queue.dropped=0;
  mutexUnlock(&queue_lock);
}

void nx_log_flush(void) {
  if(collecting)return; /* Never wait on a suspended producer, even on abort. */
  mutexLock(&writer_lock);
  mutexLock(&queue_lock);
  unsigned dropped;size_t size=nx_log_take(&queue,batch,&dropped);
  mutexUnlock(&queue_lock);
  if(output) {
    if(size)fwrite(batch,1,size,output);
    if(dropped)fprintf(output,"[log] buffer full: dropped %u complete diagnostic messages\n",dropped);
    if(size || dropped || !asynchronous)fflush(output);
  }
  mutexUnlock(&writer_lock);
}
static void run(void *unused) {
  (void)unused;
  for(;;){svcSleepThread(1000000000LL);nx_log_flush();}
}
int nx_log_printf(const char *format,va_list args) {
  if(collecting) {
    int n=vsnprintf(gc_line,sizeof gc_line,format,args);
    size_t count=n<0?0:(size_t)n;
    if(count>=sizeof gc_line){count=sizeof gc_line-1;gc_line[count-1]='\n';}
    nx_log_push(&gc_queue,gc_line,count);
    return 0;
  }
  mutexLock(&queue_lock);
  if(!initialized) {
    initialized=1;output=fopen(game_log_path(),"a");
    if(output) {
      Result rc=threadCreate(&writer,run,NULL,stack,sizeof stack,0x3b,-2);
      if(R_SUCCEEDED(rc)) {
        rc=threadStart(&writer);
        if(R_SUCCEEDED(rc))asynchronous=1;else threadClose(&writer);
      }
    }
  }
retry:
  line[0]=0;
  va_list copy;va_copy(copy,args);
  int n=vsnprintf(line,sizeof line,format,copy);va_end(copy);
  size_t count=n<0?0:(size_t)n;
  if(count>=sizeof line){count=sizeof line-1;line[count-1]='\n';}
  int critical=strstr(line,"[wd]") || strstr(line,"[nullcheck]") || strstr(line,"[bounds]") || strstr(line,"[throw-trace]") ||
    strstr(line,"FAIL") || strstr(line,"fatal") || strstr(line,"abort") || strstr(line,"Exception");
  if(output && asynchronous) {
    if(!nx_log_push(&queue,line,count) && critical) {
      queue.dropped--; /* Retried below; this critical message is not lost. */
      /* A fatal message must not disappear behind a full ordinary log queue.
       * Reformat after draining: another producer can reuse the shared line. */
      mutexUnlock(&queue_lock);nx_log_flush();mutexLock(&queue_lock);goto retry;
    }
  }
  else if(output){
    fwrite(line,1,count,output);
    uint64_t now=armGetSystemTick();
    if(!fallback_flush || critical || now-fallback_flush>=armNsToTicks(1000000000ULL)){
      fflush(output);fallback_flush=now;
    }
  }
  mutexUnlock(&queue_lock);
  if(asynchronous && critical)nx_log_flush();
  return 0;
}
#else
void nx_log_gc_enter(void){}
void nx_log_gc_leave(void){}
void nx_log_flush(void){}
int nx_log_printf(const char *format,va_list args){(void)format;(void)args;return 0;}
#endif
