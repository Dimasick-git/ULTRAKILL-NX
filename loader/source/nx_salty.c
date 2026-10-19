#include <switch.h>
#include <string.h>
#include "nx_salty.h"
#include "nx_render_control.h"
#include "util.h"

static SharedMemory memory;
static volatile NxSaltyBlock *block;
static uint64_t previous,window,pacing;
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
  if (block || foreign_producer || attempts>=30) return;
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
      for (unsigned n=0;n+sizeof(NxSaltyBlock)<=0x1000;n+=4) {
        uint32_t magic;memcpy(&magic,base+n,4);
        if (magic==0x465053 && base[n+9]) {
          foreign_producer=true;stage="existing active producer";
          rc=MAKERESULT(Module_Libnx,1);break;
        }
      }
      if (R_SUCCEEDED(rc)) {
        stage="reserve shared block";
        rc=serviceDispatchInOut(&service,6,size,offset,.in_send_pid=true);
      }
      if (R_SUCCEEDED(rc) && offset<=0x1000-size && !(offset&3)) {
        block=(volatile NxSaltyBlock *)(base+offset);
        memset((void *)block,0,sizeof(*block));
        block->fps=255;block->average=255;block->api=2;
        block->expected_buffers=-1;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        block->magic=0x465053;block->active=1;
        nx_render_shared_init((void*)(base+offset+312));
        uint64_t hz=0;
        Result rr=serviceDispatchOut(&service,10,hz,.in_send_pid=true);
        debugPrintf("[saltynx] reported display refresh=%lu Hz rc=0x%x\n",(unsigned long)hz,rr);
        if (R_SUCCEEDED(rr) && hz>60 && appletGetOperationMode()!=AppletOperationMode_Console) {
          const uint64_t safe_hz=60;
          Result sr=serviceDispatchIn(&service,11,safe_hz,.in_send_pid=true);
          debugPrintf("[saltynx] restore handheld display to 60 Hz request rc=0x%x\n",sr);
          rr=serviceDispatchOut(&service,10,hz,.in_send_pid=true);
          debugPrintf("[saltynx] verified display refresh=%lu Hz rc=0x%x\n",(unsigned long)hz,rr);
        }
        if (R_SUCCEEDED(rr) && hz>=30 && hz<=240) block->refresh=hz;
        debugPrintf("[saltynx] direct NX-FPS bridge mapped offset=%lu ABI=310; API=EGL attempt=%u\n",(unsigned long)offset,attempts);
      } else {if(R_SUCCEEDED(rc))rc=MAKERESULT(Module_Libnx,2);shmemClose(&memory);}
    } else shmemClose(&memory);
  }
  serviceDispatch(&service,0);serviceClose(&service);
  if (!block && (attempts==1 || attempts==5 || attempts==30 || foreign_producer))
    debugPrintf("[saltynx] bridge stage=%s rc=0x%x attempt=%u\n",stage,rc,attempts);
}
void nx_salty_present(uint64_t tick) {
  if (!block) {
    if (enabled && !foreign_producer && attempts<30 && tick>=retry_at) nx_salty_init();
    return;
  }
  block->magic=0x465053;block->active=1;block->api=2;
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
  if (target>60) {
    if(appletGetOperationMode()==AppletOperationMode_Console) block->locked_docked=60;
    else block->locked=60;
    target=60;
  }
  if (target>=15 && target<=60) {
    uint64_t period=19200000/target;
    uint64_t now=armGetSystemTick();
    if (pacing && now>=pacing && now-pacing<period)
      svcSleepThread(armTicksToNs(period-(now-pacing)));
    pacing=armGetSystemTick();
  } else pacing=0;
}
