"""Measure selected generated game methods without changing their logic."""
from pathlib import Path
import argparse,re
TARGETS=('CameraFrustumTargeter_Update','CameraFrustumTargeter_LateUpdate',
         'CameraController_FixedUpdate','NewMovement_Update','NewMovement_FixedUpdate',
         'GunControl_Update','EnemyIdentifier_UpdateTarget','PostProcessV2_Handler_LateUpdate',
         'BloodsplatterManager_Update','GoreZone_SlowUpdate','Flammable_Update',
         'ActivateArena_SpawnEnemy','EnemyIdentifier_Awake','EnemyIdentifier_OnEnable',
         'EnemyIdentifier_Start','SpawnableInstance_Awake','SpawnableInstance_BaseSave',
         'GoreSplatter_StopGore','EnemyIdentifier_Update','Zombie_Update','Zombie_FixedUpdate',
         'Drone_Update','Drone_FixedUpdate','GroundCheckEnemy_FixedUpdate')
HEADER=r'''#pragma once
#include <dlfcn.h>
#ifndef NX_SCOPE_TICKS
static inline unsigned long long nx_scope_ticks() {
    unsigned long long value;
    // Use the same EL0 physical counter as libnx armGetSystemTick.
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(value) : : "memory");
    return value;
}
#define NX_SCOPE_TICKS() nx_scope_ticks()
#endif
// Counter ticks, not milliseconds. Inclusive elapsed time includes any wait in
// the method; nested scopes must not be added together as independent CPU work.
class NxPortScope {
    unsigned id;
    unsigned long long start;
public:
    explicit NxPortScope(unsigned id) : id(id), start(0) {
        typedef int (*Config)(const char*);
        static Config config=(Config)dlsym(RTLD_DEFAULT,"nxb_config_int");
        static const bool enabled=config && config("engine_profiler") != 0;
        if(enabled) start=NX_SCOPE_TICKS();
    }
    ~NxPortScope() {
        if(!start) return;
        typedef void (*Record)(unsigned,unsigned long long);
        static Record record=(Record)dlsym(RTLD_DEFAULT,"nxb_scope_record");
        if(record)record(id,NX_SCOPE_TICKS()-start);
    }
};
'''

def patch(root):
    plans={};found={}
    for p in root.glob('Assembly-CSharp*.cpp'):
        text=re.sub(r'^\ufeff','',p.read_text(encoding='utf-8-sig'),flags=re.M)
        for index,name in enumerate(TARGETS):
            pattern=r'^(IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR [^\n;]*\b'+name+r'_m[0-9A-Fa-f]+\s*\([^\n;]*\)\s*\n\{)'
            matches=list(re.finditer(pattern,text,re.M))
            if not matches:continue
            assert len(matches)==1 and name not in found, 'Ambiguous method: '+name
            found[name]=p.name
            statement=f'\n\tNxPortScope nx_scope({index}); // NX_CPU_SCOPE'
            pos=matches[0].end()
            # Other patches insert guards at the entry. A previous scope can
            # therefore move away from the first line; adding another one is
            # both a C++ redeclaration and double instrumentation.
            end=text.index('\n}',pos)
            body=text[pos:end]
            old=re.findall(r'\n[ \t]*NxPortScope nx_scope\((\d+)\); // NX_CPU_SCOPE',body)
            assert all(int(sid)==index for sid in old), 'Wrong scope ID: '+name
            body=re.sub(r'\n[ \t]*NxPortScope nx_scope\(\d+\); // NX_CPU_SCOPE','',body)
            text=text[:pos]+statement+body+text[end:]
        if p.name in found.values():
            if '#include "NxPortScope.h"' not in text:text='#include "NxPortScope.h"\n'+text
            plans[p]=text
    assert set(found)==set(TARGETS), 'Missing methods: '+str(set(TARGETS)-set(found))
    (root/'NxPortScope.h').write_text(HEADER,encoding='utf-8')
    for p,text in plans.items():p.write_text(text,encoding='utf-8')
    return found

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('generated',type=Path);a=ap.parse_args()
    print('Instrumented inclusive scopes:',patch(a.generated))
