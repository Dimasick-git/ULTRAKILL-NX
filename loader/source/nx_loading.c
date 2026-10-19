#include <switch.h>
#include <stdlib.h>
#include "config.h"
#include "util.h"
#include "nx_loading.h"

static Thread worker;
static unsigned char stack[0x4000] __attribute__((aligned(0x1000)));
static Mutex lock;
static AppletHookCookie focus;
static bool ready,active,restore_requested;
static unsigned restore_failures;
static volatile bool running;
static u64 deadline;

void nxb_loading(int wanted) {
  if (!ready) return;
  mutexLock(&lock);
  bool next=wanted && appletGetFocusState()==AppletFocusState_InFocus;
  if (next && !active) {
    Result rc=appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    debugPrintf("[loading-boost] FastLoad request rc=0x%x (CPU boost / GPU throttle)\n",rc);
    if (R_SUCCEEDED(rc)) { active=true;restore_requested=false;restore_failures=0;deadline=armGetSystemTick()+60ull*19200000; }
  } else if (!next && active) {
    restore_requested=true;
    Result rc=appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    debugPrintf("[loading-boost] restore normal rc=0x%x\n",rc);
    if (R_SUCCEEDED(rc)) active=false;
  }
  mutexUnlock(&lock);
}
static void timer(void *unused) {
  (void)unused;
  while (__atomic_load_n(&running,__ATOMIC_ACQUIRE)) {
    mutexLock(&lock);
    if (active && (restore_requested || armGetSystemTick()>=deadline || appletGetFocusState()!=AppletFocusState_InFocus)) {
      Result rc=appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
      if (R_SUCCEEDED(rc) || ++restore_failures<=2 || !(restore_failures%64))
        debugPrintf("[loading-boost] timer/focus/pending restore rc=0x%x\n",rc);
      if (R_SUCCEEDED(rc)) active=false;
    }
    mutexUnlock(&lock);
    svcSleepThread(50000000);
  }
}
static void focus_changed(AppletHookType type,void *arg) {
  (void)arg;
  if (type==AppletHookType_OnFocusState && appletGetFocusState()!=AppletFocusState_InFocus)
    nxb_loading(0);
}
static void finish(void) {
  if (!ready) return;
  __atomic_store_n(&running,false,__ATOMIC_RELEASE);
  threadWaitForExit(&worker);threadClose(&worker);
  nxb_loading(0);appletUnhook(&focus);ready=false;
}
void nx_loading_init(void) {
  if (!config.loading_boost) return;
  mutexInit(&lock);running=true;
  Result rc=threadCreate(&worker,timer,NULL,stack,sizeof(stack),0x2c,-2);
  if (R_FAILED(rc)) {running=false;debugPrintf("[loading-boost] timer unavailable rc=0x%x; disabled\n",rc);return;}
  rc=threadStart(&worker);
  if (R_FAILED(rc)) {running=false;threadClose(&worker);debugPrintf("[loading-boost] timer start rc=0x%x; disabled\n",rc);return;}
  ready=true;appletHook(&focus,focus_changed,NULL);atexit(finish);
  nxb_loading(1);
}
