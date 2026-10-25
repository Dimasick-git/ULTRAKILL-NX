/* Bounded wall-clock timings at actual generated method entry/exit. No managed
 * allocations, per-call logging or sampling of another thread's stack. */
#include <switch.h>
#include <stdint.h>
#include <string.h>
#include "util.h"
#include "config.h"
static const char *names[]={"CameraFrustumTargeter_Update","CameraFrustumTargeter_LateUpdate",
 "CameraController_FixedUpdate","NewMovement_Update","NewMovement_FixedUpdate",
 "GunControl_Update","EnemyIdentifier_UpdateTarget","PostProcessV2_Handler_LateUpdate",
 "BloodsplatterManager_Update","GoreZone_SlowUpdate","Flammable_Update",
 "ActivateArena_SpawnEnemy","EnemyIdentifier_Awake","EnemyIdentifier_OnEnable",
 "EnemyIdentifier_Start","SpawnableInstance_Awake","SpawnableInstance_BaseSave",
 "GoreSplatter_StopGore","EnemyIdentifier_Update","Zombie_Update","Zombie_FixedUpdate",
 "Drone_Update","Drone_FixedUpdate","GroundCheckEnemy_FixedUpdate",
 /* управляемые колбэки камер (Camera.onPreCull/onPreRender/onPostRender) -- до
  * 3.9 мс у Main Camera; номера -- из tools/patch_nx_game_fixes.py */
 "TMP_UpdateManager_DoRebuilds","PostProcessV2_Handler_OnPreRenderCallback",
 "PortalManagerV2_OnPreRenderCallback","PostProcessV2_Handler_RenderLimboSkyboxes",
 "PostProcessV2_Handler_RenderSpaceSky","PortalRenderV2_Render","NxFsr_Rendered"};
typedef struct {uint64_t count,ticks,max;} Scope;
static Scope scopes[sizeof names/sizeof *names];
static Mutex scope_lock;
static unsigned slow_spawn_samples;
void nxb_scope_record(unsigned id,unsigned long long ticks) {
  if(!config.engine_profiler || id>=sizeof scopes/sizeof *scopes)return;
  mutexLock(&scope_lock);
  Scope *s=&scopes[id];s->count++;s->ticks+=ticks;if(ticks>s->max)s->max=ticks;
  int slow=id>=11 && id<=16 && ticks>=armNsToTicks(4000000ULL) && slow_spawn_samples<8;
  if(slow)slow_spawn_samples++;
  mutexUnlock(&scope_lock);
  if(slow)debugPrintf("[spawn-cost] %s %.2fms inclusive\n",names[id],(double)armTicksToNs(ticks)/1e6);
}
void nxb_scope_poll(void) {
  if(!config.engine_profiler)return;
  static uint64_t previous;
  uint64_t now=armGetSystemTick();
  if(!previous){previous=now;return;}
  if(now-previous<armNsToTicks(5000000000ULL))return;
  double seconds=(double)armTicksToNs(now-previous)/1e9;previous=now;
  Scope copy[sizeof scopes/sizeof *scopes];
  mutexLock(&scope_lock);memcpy(copy,scopes,sizeof copy);memset(scopes,0,sizeof scopes);slow_spawn_samples=0;mutexUnlock(&scope_lock);
  for(unsigned i=0;i<sizeof copy/sizeof *copy;++i)if(copy[i].count)
    debugPrintf("[cpu-scope] %s calls=%lu mean=%.3fms max=%.3fms elapsed=%.1fms window=%.2fs inclusive\n",
      names[i],(unsigned long)copy[i].count,(double)armTicksToNs(copy[i].ticks)/1e6/copy[i].count,
      (double)armTicksToNs(copy[i].max)/1e6,(double)armTicksToNs(copy[i].ticks)/1e6,seconds);
}
