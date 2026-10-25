"""Use allocation-free GetContact in five audited contact readers.

Retain contact order, count, normals/points and collision response. Do not
enable global callback reuse: Collision*Message forwards Collision through
serialized UnityEvents, whose listeners may retain it.
"""
from pathlib import Path
import argparse,re
MARKER='// NX_CONTACTS_NO_ARRAY'
TARGETS={'ClimbStep_OnCollisionStay':1,'NewMovement_OnCollisionEnter':1,
         'MaliciousFace_HandleCollision':6,'SpiderBody_OnCollisionEnter':6,'Zapper_OnCollisionEnter':1}

def patch_body(body,get,count,collision,contact):
    if MARKER in body:return body
    original=body
    arrays=re.findall(r'ContactPointU5BU5D_\w+\* (L_\d+);\n\s*\1 = Collision_get_contacts_\w+\((L_\d+), NULL\);',body)
    assert arrays,'Missing contacts'
    if ')->GetAt(' in body:
        # The two foreach readers only use the array locally, never store it.
        assert len(arrays)==1
        body,n=re.subn(r'ContactPointU5BU5D_\w+\*',collision+'*',body)
        assert n==4,n # stored array, creation, element alias, length alias
        body,n=re.subn(r'Collision_get_contacts_\w+\((L_\d+), NULL\)',r'\1',body);assert n==1
        body,n=re.subn(r'\((L_\d+)\)->GetAt\(static_cast<il2cpp_array_size_t>\((L_\d+)\)\)',lambda m:f'{get}({m[1]}, {m[2]}, NULL)',body);assert n==1
        body,n=re.subn(r'\(\(\(RuntimeArray\*\)(L_\d+)\)->max_length\)',lambda m:f'{count}({m[1]}, NULL)',body);assert n==1
    else:
        for array,source in arrays:
            declaration=re.search(r'ContactPointU5BU5D_\w+\* '+array+r';',body)[0]
            body=body.replace(declaration,contact+' '+array+';',1)
            body,n=re.subn(array+r' = Collision_get_contacts_\w+\('+source+r', NULL\);',f'{array} = {get}({source}, 0, NULL);',body);assert n==1
            assert body.count(f'NullCheck({array});')==1
            body=body.replace(f'\t\tNullCheck({array});\n','',1)
            address=f'(({array})->GetAddressAt(static_cast<il2cpp_array_size_t>(0)))'
            assert body.count(address)==1
            body=body.replace(address,f'(&{array})',1)
    assert 'Collision_get_contacts_' not in body and 'ContactPointU5BU5D_' not in body
    # No other statement is removed; the loop/body/response is retained.
    return body.replace('\n{','\n{\n\t'+MARKER,1)

def patch(root):
    engine=(root/'UnityEngine.PhysicsModule.cpp').read_text(encoding='utf-8-sig')
    get_decl=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR (ContactPoint_\w+) (Collision_GetContact_\w+) \((Collision_\w+)\*[^\n]+\)',engine,re.M)
    count_decl=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR int32_t (Collision_get_contactCount_\w+) \([^\n]+\)',engine,re.M)
    assert get_decl and count_decl
    contact,get,collision=get_decl.groups();count=count_decl[1]
    header='\n'+get_decl[0]+';\n'+count_decl[0]+';\n'
    found={};plans={}
    for p in root.glob('Assembly-CSharp*.cpp'):
        text=p.read_text(encoding='utf-8-sig');changed=False
        for name,expected in TARGETS.items():
            m=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void '+name+r'_m[^\n;]+\n\{[\s\S]*?(?=^// Method Definition Index:)',text,re.M)
            if not m:continue
            assert name not in found;found[name]=p.name
            if MARKER not in m[0]:assert m[0].count('Collision_get_contacts_')==expected
            result=patch_body(m[0],get,count,collision,contact)
            text=text[:m.start()]+result+text[m.end():];changed=True
        if changed:
            if header not in text:
                # Types must already be declared; put declarations immediately
                # before the first function implementation, after type structs.
                pos=re.search(r'^// Method Definition Index:',text,re.M).start()
                text=text[:pos]+header+text[pos:]
            plans[p]=text
    assert set(found)==set(TARGETS),found
    for p,text in plans.items():p.write_text(text,encoding='utf-8')
    return found

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('generated',type=Path);a=ap.parse_args()
    print('Allocation-free contact readers:',patch(a.generated))
