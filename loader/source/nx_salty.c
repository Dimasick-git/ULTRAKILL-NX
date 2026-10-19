#include <switch.h>
#include <string.h>
#include "nx_salty.h"
#include "util.h"

static SharedMemory memory;
static volatile NxSaltyBlock *block;
static uint64_t previous,window,pacing;
static unsigned frames,sample_index;
void nx_salty_init(void) {
  Handle handle=0,remote=0; uint64_t offset=0,size=sizeof(NxSaltyBlock);
  /* InjectServ is only a wake-up port: the server immediately closes it.
   * Shared-memory commands are served on the SaltySD session. */
  Result rc=svcConnectToNamedPort(&handle,"SaltySD");
  if (R_FAILED(rc)) {debugPrintf("[saltynx] unavailable rc=0x%x; normal presentation retained\n",rc);return;}
  Service service;serviceCreate(&service,handle);
  rc=serviceDispatch(&service,7,.in_send_pid=true,.out_handle_attrs={SfOutHandleAttr_HipcCopy},.out_handles=&remote);
  if (R_SUCCEEDED(rc)) {
    shmemLoadRemote(&memory,remote,0x1000,Perm_Rw);
    rc=shmemMap(&memory);
    if (R_SUCCEEDED(rc)) {
      const uint8_t *base=shmemGetAddr(&memory);
      for (unsigned n=0;n+sizeof(NxSaltyBlock)<=0x1000;n+=4) {
        uint32_t magic;memcpy(&magic,base+n,4);
        if (magic==0x465053 && base[n+9]) {rc=MAKERESULT(Module_Libnx,1);break;}
      }
      if (R_SUCCEEDED(rc)) rc=serviceDispatchInOut(&service,6,size,offset,.in_send_pid=true);
      if (R_SUCCEEDED(rc) && offset<=0x1000-size && !(offset&3)) {
        block=(volatile NxSaltyBlock *)(base+offset);
        memset((void *)block,0,sizeof(*block));
        block->fps=255;block->average=255;block->api=2;
        block->expected_buffers=-1;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        block->magic=0x465053;block->active=1;
        debugPrintf("[saltynx] direct NX-FPS bridge mapped offset=%lu ABI=310; API=EGL\n",(unsigned long)offset);
      } else {if(R_SUCCEEDED(rc))rc=MAKERESULT(Module_Libnx,2);shmemClose(&memory);}
    } else shmemClose(&memory);
  }
  serviceDispatch(&service,0);serviceClose(&service);
  if (!block) debugPrintf("[saltynx] bridge skipped rc=0x%x (existing producer or IPC/map failure)\n",rc);
}
void nx_salty_present(uint64_t tick) {
  if (!block) return;
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
  if (target>=15 && target<=60) {
    uint64_t period=19200000/target;
    uint64_t now=armGetSystemTick();
    if (pacing && now>=pacing && now-pacing<period)
      svcSleepThread(armTicksToNs(period-(now-pacing)));
    pacing=armGetSystemTick();
  } else pacing=0;
}
