#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "config.h"
#include "util.h"
#include "nx_render_control.h"

static NxFsrShared *shared;
static NxFsrLive *live;
static int height,filter;
static float sharpness;
static uint32_t revision;
static int local_request;

void nx_render_config_changed(void) {
  height=config.render_height;filter=config.fsr;sharpness=config.fsr_sharpness;
  local_request=1;
}

static int limited_height(int percent) {
  if (percent<25) percent=25;
  if (percent>100) percent=100;
  return (720*percent+50)/100;
}
static void defaults(void) {
  if (height) return;
  height=config.render_height;filter=config.fsr;sharpness=config.fsr_sharpness;
}
void nx_render_shared_init(void *region) {
  defaults();
  /* Honor this title's persisted external request when it differs from its
   * disabled/100% template; otherwise retain the tested game profile. */
  FILE *f=fopen("sdmc:/SaltySD/plugins/FSR/01005CE3B1A40000.ini","r");
  int scale=100,on=0;float stops=sharpness;
  if (f) {
    char line[256],key[64],value[64];
    while(fgets(line,sizeof(line),f)) if(sscanf(line," %63[^=]=%63s",key,value)==2) {
      if(!strcmp(key,"scale")) sscanf(value,"%d",&scale);
      if(!strcmp(key,"enabled")) sscanf(value,"%d",&on);
      if(!strcmp(key,"sharpness")) sscanf(value,"%f",&stops);
    }
    fclose(f);
    if ((on==1 || scale!=100) && scale>=15 && scale<=200 && stops>=0 && stops<=2) {
      height=limited_height(scale);filter=on!=0;sharpness=stops;
    }
  }
  shared=region;live=(void*)((char*)region+sizeof(*shared));
  memset(region,0,sizeof(*shared)+sizeof(*live));
  shared->version=3;shared->sharpness=sharpness;shared->status=1;
  shared->scale_percent=(height*100+360)/720;
  strcpy(shared->log,"Unity GLES backend: EASU+RCAS / plain; scale 25..100%; Lite/NVN options unavailable");
  shared->log_seq=1;
  live->scale=shared->scale_percent;live->max_scale=100;live->fsr=filter;
  live->active=5; /* enabled runtime scaling + coherent seq bit31 transport */
  live->upscaler=0x80; /* EASU+RCAS is the implemented filter */
  __atomic_thread_fence(__ATOMIC_RELEASE);
  shared->magic=0x31525346;live->magic=0x43525346;
  debugPrintf("[fsr-control] FSR1 v3/FSRC mapped; %dp filter=%d; 25..100%% GLES backend\n",height,filter);
}
static void poll(void) {
  defaults();if(!live)return;
  if (local_request) {
    uint32_t old=__atomic_load_n(&live->seq,__ATOMIC_ACQUIRE);
    if (old&0x80000000u) return;
    if(!__atomic_compare_exchange_n(&live->seq,&old,old|0x80000000u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return;
    live->scale=(height*100+360)/720;live->fsr=filter;
    shared->sharpness=sharpness;
    revision=((old+1)&0x7fffffffu);if(!revision)revision=1;
    local_request=0;
    __atomic_store_n(&live->seq,revision,__ATOMIC_RELEASE);
    return;
  }
  uint32_t seq=__atomic_load_n(&live->seq,__ATOMIC_ACQUIRE);
  if(!seq || (seq&0x80000000u) || seq==revision)return;
  uint16_t percent=__atomic_load_n(&live->scale,__ATOMIC_RELAXED);
  uint8_t on=__atomic_load_n(&live->fsr,__ATOMIC_RELAXED);
  float stops;__atomic_load(&shared->sharpness,&stops,__ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  if(seq!=__atomic_load_n(&live->seq,__ATOMIC_ACQUIRE))return;
  if(percent<15 || percent>200 || on>1 || !(stops>=0 && stops<=2))return;
  uint32_t expected=seq;
  if(!__atomic_compare_exchange_n(&live->seq,&expected,seq|0x80000000u,false,
                                  __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
  /* Report the supported effective request, rather than acknowledging Lite,
   * HDR or NVN pipeline changes that this GLES backend did not perform. */
  live->scale=percent<25?25:percent>100?100:percent;
  live->upscaler=0x80;live->native=0x80;live->hdr=0x80;
  live->depth=live->lazy=live->shader_fix=0;
  __atomic_store_n(&live->seq,seq,__ATOMIC_RELEASE);
  height=limited_height(percent);filter=on;sharpness=stops;revision=seq;
  shared->status=1;
  debugPrintf("[fsr-control] request=%u scale=%u -> %dp filter=%d sharpness=%.2f; pending render\n",
              seq,percent,height,filter,(double)stops);
}
int nx_render_height(void){poll();return height;}
/* NxFsr reads height, then sharpness/filter on the same Unity thread. Latch
 * once at the first getter so a concurrent overlay publication cannot mix
 * revisions across those three P/Invoke calls. */
int nx_render_filter(void){defaults();return filter;}
float nx_render_sharpness(void){defaults();return sharpness;}
void nxb_fsr_applied(int w,int h,int ow,int oh,int active) {
  if(!shared)return;
  shared->in_width=w;shared->in_height=h;shared->out_width=ow;shared->out_height=oh;
  shared->frames++;shared->enabled=active!=0;shared->scaled_count=h<oh?1:0;
  shared->scale_percent=oh>0?(h*100+oh/2)/oh:0;
  shared->status=active?2:0;
  /* A newly requested size is acknowledged only after the composition camera
   * actually submitted that size; receipt alone is not application. */
  if(h==height && active==filter && ow>0 && oh==720)
    __atomic_store_n(&live->applied,revision,__ATOMIC_RELEASE);
}
