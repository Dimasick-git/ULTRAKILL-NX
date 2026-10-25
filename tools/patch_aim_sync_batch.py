"""Synchronize once for a read-only autoaim scan; restore on every C++ exit.

This is deliberately local. Global autoSyncTransforms remains unchanged. The
two target bodies only read world state; portal traversal performs cast-state
math, and the only setters update target fields or a UI RectTransform.
"""
from pathlib import Path
import argparse,re

MARKER='// NX_AIM_SYNC_BATCH'

def patch(root):
    managed='\n'.join(p.read_text(encoding='utf-8-sig') for p in root.glob('NxPort*.cpp'))
    declarations=[]
    for name,ret,args in [('BeginAimRead','bool','const RuntimeMethod* method'),
                          ('EndAimRead','void','bool restore, const RuntimeMethod* method')]:
        hits=re.findall(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR '+ret+r' (NxPhysicsQueries_'+name+r'_m[0-9A-F]+)\s*\([^\n;]*\)\s*\n\{',managed,re.M)
        assert len(hits)==1, (name,hits)
        declarations.append((hits[0],f'IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR {ret} {hits[0]}({args});'))
    header='#pragma once\n'+ '\n'.join(d[1] for d in declarations)+f'''
class NxAimReadBatch {{
    bool restore;
public:
    NxAimReadBatch() : restore({declarations[0][0]}(NULL)) {{}}
    ~NxAimReadBatch() {{ {declarations[1][0]}(restore,NULL); }}
    NxAimReadBatch(const NxAimReadBatch&) = delete;
    NxAimReadBatch& operator=(const NxAimReadBatch&) = delete;
}};
'''
    plans={};found=[]
    for p in root.glob('Assembly-CSharp*.cpp'):
        text=p.read_text(encoding='utf-8-sig')
        statement='\n\tNxAimReadBatch nx_aim_read; '+MARKER
        text=text.replace(statement,'') # Relocate older unconditional LateUpdate scopes.
        for name in ('CameraFrustumTargeter_Update','CameraFrustumTargeter_LateUpdate'):
            matches=list(re.finditer(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void '+name+r'_m[^\n;]+\n\{',text,re.M))
            if not matches:continue
            assert len(matches)==1 and name not in found;found.append(name)
            m=matches[0]
            pos=m.end()
            if name.endswith('_LateUpdate'):
                end=text.index('// Method Definition Index:',pos)
                body=text[pos:end]
                anchor='\nIL_0027:\n\t{'
                assert body.count(anchor)==1 and body.index('Collider_get_bounds_')>body.index(anchor)
                pos+=body.index(anchor)+len(anchor)
            else:
                scope=re.match(r'\n\tNxPortScope nx_scope\(\d+\); // NX_CPU_SCOPE',text[pos:])
                if scope:pos+=scope.end() # Include synchronization in the existing method timing.
            if not text[pos:].startswith(statement):text=text[:pos]+statement+text[pos:]
        if any(n in text for n in found) and MARKER in text:
            text=text.replace('#include "NxAimReadBatch.h"\n','')
            assert '#include "pch-cpp.hpp"' in text
            text=text.replace('#include "pch-cpp.hpp"','#include "pch-cpp.hpp"\n#include "NxAimReadBatch.h"',1)
            plans[p]=text
    assert len(found)==2,found
    (root/'NxAimReadBatch.h').write_text(header,encoding='utf-8')
    for p,text in plans.items():p.write_text(text,encoding='utf-8')
    return found

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('generated',type=Path);a=ap.parse_args()
    print('Local autoaim physics sync batch:',patch(a.generated))
