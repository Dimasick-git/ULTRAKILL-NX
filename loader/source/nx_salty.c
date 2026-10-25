#include <switch.h>
#include <string.h>
#include "config.h"
#include "nx_salty.h"
#include "nx_render_control.h"
#include "util.h"

static SharedMemory memory;
static volatile NxSaltyBlock *block;
static volatile uint8_t *shared_base;
static uint64_t previous,window;
static unsigned overlay_limit;
int nx_salty_fps_limit(void) { return (int)__atomic_load_n(&overlay_limit, __ATOMIC_RELAXED); }
static unsigned frames,sample_index;
static bool enabled,foreign_producer,limit_checked;
static unsigned attempts;
static uint64_t retry_at;

static bool nx_salty_session_room(void) {
  u64 raw=0; s64 limit=-1,current=-1;
  Result rc=svcGetInfo(&raw,InfoType_ResourceLimit,INVALID_HANDLE,0);
  if (R_FAILED(rc) || !raw) {
    debugPrintf("[saltynx] session resource handle rc=0x%x\n",rc);return false;
  }
  Handle resource=(Handle)raw;
  Result lr=svcGetResourceLimitLimitValue(&limit,resource,LimitableResource_Sessions);
  Result cr=svcGetResourceLimitCurrentValue(&current,resource,LimitableResource_Sessions);
  bool changed=false;
  debugPrintf("[saltynx] process sessions current=%lld limit=%lld query=0x%x/0x%x\n",
              (long long)current,(long long)limit,lr,cr);
  /* Only repair an actually exhausted small process budget; do not alter
   * memory/thread budgets or guess that every 0x10801 is a session limit. */
  if (R_SUCCEEDED(lr) && R_SUCCEEDED(cr) && current>=limit && limit>=0 && limit<=128) {
    rc=svcSetResourceLimitLimitValue(resource,LimitableResource_Sessions,current+8);
    changed=R_SUCCEEDED(rc);
    debugPrintf("[saltynx] session budget request %lld -> %lld rc=0x%x\n",
                (long long)limit,(long long)(current+8),rc);
  }
  svcCloseHandle(resource);
  return changed;
}
void nx_salty_init(void) {
  enabled=true;
  if (block || attempts>=30) return;
  foreign_producer=false;
  ++attempts;
  Handle handle=0,remote=0; uint64_t offset=0,size=312+sizeof(NxFsrShared)+sizeof(NxFsrLive);
  /* InjectServ is only a wake-up port: the server immediately closes it.
   * Shared-memory commands are served on the SaltySD session. */
  Result rc=svcConnectToNamedPort(&handle,"SaltySD");
  if (rc==0x10801 && !limit_checked) {
    limit_checked=true;
    if (nx_salty_session_room()) rc=svcConnectToNamedPort(&handle,"SaltySD");
  }
  retry_at=armGetSystemTick()+19200000;
  if (R_FAILED(rc)) {
    if (attempts==1 || attempts==5 || attempts==30)
      debugPrintf("[saltynx] connect SaltySD attempt=%u rc=0x%x; %s\n",attempts,rc,
                  attempts<30?"retry on present in 1s":"retry budget exhausted");
    return;
  }
  Service service;serviceCreate(&service,handle);
  const char *stage="get shared handle";
  rc=serviceDispatch(&service,7,.in_send_pid=true,.out_handle_attrs={SfOutHandleAttr_HipcCopy},.out_handles=&remote);
  if (R_SUCCEEDED(rc)) {
    shmemLoadRemote(&memory,remote,0x1000,Perm_Rw);
    stage="map shared memory";
    rc=shmemMap(&memory);
    if (R_SUCCEEDED(rc)) {
      const uint8_t *base=shmemGetAddr(&memory);
      shared_base=(volatile uint8_t*)base;
      /* Bytes 0..3 belong to SaltyNX (operation mode / cached display Hz).
       * r90 placed the FPS magic at zero: byte 1 of 0x465053 is 0x50 = 80.
       * Invalidate only that malformed legacy header, then let the sysmodule
       * repopulate its refresh cache; do not invent a measured display rate. */
      uint32_t legacy_magic;memcpy(&legacy_magic,base,4);
      const NxFsrShared *legacy_control=(const void*)(base+312);
      const int damaged_legacy=base[2]==0x46 && base[3]==0 &&
          legacy_control->magic==0x31525346 && legacy_control->version==3 &&
          !memcmp(legacy_control->log,"Unity GLES backend:",19);
      if(legacy_magic==0x465053 || damaged_legacy) {
        memset((void*)base,0,4);
        shared_base[0]=appletGetOperationMode()==AppletOperationMode_Console;
        debugPrintf("[saltynx] repaired legacy FPS/header overlap; awaiting display refresh cache\n");
      }
      int existing=-1,control=-1,ours=0;
      for (unsigned n=4;n+sizeof(NxSaltyBlock)<=0x1000;n+=4) {
        uint32_t magic;memcpy(&magic,base+n,4);
        if (magic==0x465053 && existing<0) {
          existing=(int)n;
          /* Older versions left their active bit set at process exit. Reuse
           * only our recognisable full region; never append another FPS magic
           * behind one that Status Monitor will find first. */
          if(n+312<=0x1000 && base[n+310]=='N' && base[n+311]=='X') ours=1;
          if(!ours && n+size<=0x1000) {
            const NxFsrShared *s=(const void*)(base+n+312);
            const NxFsrLive *l=(const void*)(base+n+312+sizeof(*s));
            ours=s->magic==0x31525346 && s->version==3 && l->magic==0x43525346 &&
                 !memcmp(s->log,"Unity GLES backend:",19);
          }
        }
      }
      for(unsigned n=4;n+sizeof(NxFsrShared)+sizeof(NxFsrLive)<=0x1000;n+=4) {
        const NxFsrShared *s=(const void*)(base+n);
        const NxFsrLive *l=(const void*)(base+n+sizeof(*s));
        if(s->magic==0x31525346 && s->version==3 && l->magic==0x43525346) {control=(int)n;break;}
      }
      if(existing>=0) {
        offset=(uint64_t)existing;
        /* An injected producer can own FPS without owning our GLES scaling
         * path. Attach its telemetry read-only, but still connect FSR controls. */
        foreign_producer=base[existing+9] && !ours;
        if(control<0) {
          uint64_t control_size=sizeof(NxFsrShared)+sizeof(NxFsrLive),control_offset=0;
          stage="reserve independent FSR controls";
          /* Prefix padding also protects the header if this is the first allocation. */
          control_size+=4;
          rc=serviceDispatchInOut(&service,6,control_size,control_offset,.in_send_pid=true);
          if(R_SUCCEEDED(rc))control_offset+=4;
          if(R_SUCCEEDED(rc) && control_offset<=0x1000-control_size && !(control_offset&3))control=(int)control_offset;
          else if(R_SUCCEEDED(rc))rc=MAKERESULT(Module_Libnx,2);
        }
      }
      if (R_SUCCEEDED(rc) && existing<0) {
        stage="reserve shared block";
        uint64_t reserve_size=size+4;
        rc=serviceDispatchInOut(&service,6,reserve_size,offset,.in_send_pid=true);
        if(R_SUCCEEDED(rc))offset+=4;
      }
      if (R_SUCCEEDED(rc) && offset>=4 && offset<=0x1000-(existing<0?size:sizeof(NxSaltyBlock)) && !(offset&3)) {
        block=(volatile NxSaltyBlock *)(base+offset);
        if(!foreign_producer) {
          memset((void *)block,0,sizeof(*block));
          block->fps=255;block->average=255;block->api=2;
          block->expected_buffers=-1;
          ((volatile uint8_t*)block)[310]='N';((volatile uint8_t*)block)[311]='X';
          __atomic_thread_fence(__ATOMIC_RELEASE);
          block->magic=0x465053;block->active=1;
        }
        nx_render_shared_init((void*)(base+(control>=0?(unsigned)control:offset+312)));
        uint64_t hz=0;
        Result rr=serviceDispatchOut(&service,10,hz,.in_send_pid=true);
        debugPrintf("[saltynx] reported display refresh=%lu Hz rc=0x%x\n",(unsigned long)hz,rr);
        /* Command 10 reads SaltyNX's cached header, not an immediate hardware
         * query. Publish updates from that header without any display writes. */
        if (!foreign_producer && R_SUCCEEDED(rr) && hz>=30 && hz<=240) block->refresh=hz;
        debugPrintf("[saltynx] FPS offset=%lu mode=%s FSR-control=%d; ABI=310 attempt=%u\n",(unsigned long)offset,
                    foreign_producer?"external read-only":ours?"reuse own region":"direct EGL",control>=0?control:(int)offset+312,attempts);
      } else {if(R_SUCCEEDED(rc))rc=MAKERESULT(Module_Libnx,2);shmemClose(&memory);}
    } else shmemClose(&memory);
  }
  serviceDispatch(&service,0);serviceClose(&service);
  if (!block && (attempts==1 || attempts==5 || attempts==30 || foreign_producer))
    debugPrintf("[saltynx] bridge stage=%s rc=0x%x attempt=%u\n",stage,rc,attempts);
}
void nx_salty_present(uint64_t tick) {
  if (!block) {
    if (enabled && attempts<30 && tick>=retry_at) nx_salty_init();
    return;
  }
  if(foreign_producer)return; /* Its hooks count/pacing; do not double count. */
  block->magic=0x465053;block->active=1;block->api=2;
  if(shared_base) {
    const unsigned hz=shared_base[1];
    if(hz>=30 && hz<=240 && block->refresh!=hz) {
      block->refresh=hz;
      static unsigned reported;
      if(reported++<8)debugPrintf("[saltynx] display refresh cache updated=%u Hz (independent of FPS cap)\n",hz);
    }
  }
  if (previous && tick>previous) {
    uint64_t delta=tick-previous;
    block->ticks[sample_index++%10]=(uint32_t)(delta>UINT32_MAX?UINT32_MAX:delta);
  }
  previous=tick;
  if (!window) { window=tick;frames=0; }
  else ++frames;
  ++block->frame;
  if (tick-window>=19200000) {
    float fps=(float)((double)frames*19200000.0/(tick-window));
    block->average=fps;block->fps=(uint8_t)(fps>254?254:fps+0.5f);
    frames=0;window=tick;
  }
  unsigned target=appletGetOperationMode()==AppletOperationMode_Console?block->locked_docked:block->locked;
  /* Publish the requested cap to Unity's main-thread limiter. A second
   * sleep here delays submission and creates competing pacing clocks. */
  __atomic_store_n(&overlay_limit, target >= 15 && target <= 240 ? target : 0, __ATOMIC_RELAXED);
}

void nx_salty_shutdown(void) {
  /* Render threads can still be alive at process exit. Leave the mapping in
   * place; publish inactivity only for the block this producer owns. */
  enabled=false;
  if(block && !foreign_producer) {block->active=0;__atomic_thread_fence(__ATOMIC_RELEASE);}
}
