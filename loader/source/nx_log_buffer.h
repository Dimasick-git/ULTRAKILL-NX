#pragma once
#include <stddef.h>
#include <string.h>
#define NX_LOG_CAPACITY (512*1024)
typedef struct {char bytes[NX_LOG_CAPACITY];size_t size;unsigned dropped;} NxLogBuffer;
/* Caller owns synchronization. Whole messages are retained or dropped. */
static inline int nx_log_push(NxLogBuffer *q,const char *text,size_t size) {
  if(size>sizeof q->bytes-q->size){q->dropped++;return 0;}
  memcpy(q->bytes+q->size,text,size);q->size+=size;return 1;
}
static inline size_t nx_log_take(NxLogBuffer *q,char *out,unsigned *dropped) {
  size_t size=q->size;memcpy(out,q->bytes,size);q->size=0;
  *dropped=q->dropped;q->dropped=0;return size;
}
