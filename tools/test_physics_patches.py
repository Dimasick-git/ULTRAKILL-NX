"""Production patch idempotence, unchanged logic and compiled contact/RAII paths."""
from pathlib import Path
import argparse,re,subprocess,tempfile,shutil
from patch_aim_sync_batch import patch as aim_patch,MARKER as AIM
from patch_collision_contacts import patch_body,MARKER as CONTACT,TARGETS

def main():
    ap=argparse.ArgumentParser();ap.add_argument('generated',type=Path);ap.add_argument('--cxx',required=True);a=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='nx-physics-') as tmp:
        tmp=Path(tmp)
        # Patch actual autoaim bodies without recompiling the whole game. Then
        # remove ONLY the added bracket/include and compare every original byte.
        original=(a.generated/'Assembly-CSharp__2.cpp').read_text(encoding='utf-8-sig')
        assert AIM not in original
        (tmp/'Assembly-CSharp__2.cpp').write_text(original,encoding='utf-8')
        (tmp/'NxPort.cpp').write_text('''IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR bool NxPhysicsQueries_BeginAimRead_mABCD (const RuntimeMethod* method)
{}
IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void NxPhysicsQueries_EndAimRead_mBCDE (bool restore, const RuntimeMethod* method)
{}
''')
        aim_patch(tmp);changed=(tmp/'Assembly-CSharp__2.cpp').read_text()
        assert changed.index('#include "NxAimReadBatch.h"')>changed.index('#include "pch-cpp.hpp"')
        late=changed[changed.index('IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void CameraFrustumTargeter_LateUpdate_m'):]
        late=late[:late.index('// Method Definition Index:')]
        assert late.index('NxAimReadBatch nx_aim_read')>late.index('IL_0027:')>late.index('return;')
        assert late.index('NxAimReadBatch nx_aim_read')<late.index('Collider_get_bounds_')
        aim_patch(tmp);assert changed==(tmp/'Assembly-CSharp__2.cpp').read_text()
        clean=re.sub(r'\n\tNxAimReadBatch nx_aim_read; '+re.escape(AIM),'',changed)
        clean=clean.replace('#include "NxAimReadBatch.h"\n','');assert clean==original
        fragments=[];found={}
        for p in a.generated.glob('Assembly-CSharp*.cpp'):
            text=p.read_text(encoding='utf-8-sig')
            for name,expected in TARGETS.items():
                m=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void '+name+r'_m[^\n;]+\n\{[\s\S]*?(?=^// Method Definition Index:)',text,re.M)
                if not m:continue
                old=m[0];assert CONTACT not in old;assert old.count('Collision_get_contacts_')==expected
                new=patch_body(old,'GetContact','GetCount','Collision','ContactPoint')
                assert patch_body(new,'GetContact','GetCount','Collision','ContactPoint')==new
                found[name]=expected
                # Exact production replacement expressions compiled below.
                for hit in re.finditer(r'\bGetContact\((L_\d+), (L_\d+|0), NULL\)',new):fragments.append(hit[0])
                assert new.count('goto ')==old.count('goto ') and new.count('return;')==old.count('return;')
                assert new.count('ContactPoint_get_normal_')==old.count('ContactPoint_get_normal_')
                assert new.count('ContactPoint_get_point_')==old.count('ContactPoint_get_point_')
                # Each unchanged original non-contact line is present in order.
                cursor=0
                for line in old.splitlines():
                    if any(s in line for s in ('ContactPointU5BU5D_', 'Collision_get_contacts_', '->GetAt(', '->GetAddressAt(', 'max_length')):continue
                    if re.match(r'\s*NullCheck\(L_\d+\);',line):continue
                    pos=new.find(line,cursor);assert pos>=0,(name,line);cursor=pos+len(line)
        assert found==TARGETS
        assert len(fragments)==15
        # Compile the actual generated RAII wrapper, including exception paths.
        header=(tmp/'NxAimReadBatch.h').read_text()
        cpp=r'''
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <vector>
#define IL2CPP_EXTERN_C
#define IL2CPP_METHOD_ATTR
struct RuntimeMethod {};
bool policy=true;int syncs=0;
bool NxPhysicsQueries_BeginAimRead_mABCD(const RuntimeMethod*) {if(!policy)return false;syncs++;policy=false;return true;}
void NxPhysicsQueries_EndAimRead_mBCDE(bool restore,const RuntimeMethod*) {if(restore)policy=true;}
HEADER
struct ContactPoint {int normal,point;};
struct Collision {std::vector<ContactPoint> contacts;};
ContactPoint GetContact(Collision* c,int index,void*) {return c->contacts.at(index);}
int GetCount(Collision* c,void*){return (int)c->contacts.size();}
int main(){
 try {NxAimReadBatch outer;assert(!policy && syncs==1);{NxAimReadBatch inner;assert(!policy && syncs==1);}assert(!policy);throw 7;}catch(int){}
 assert(policy && syncs==1);
 auto early=[](){NxAimReadBatch scope;return 1;};assert(early()==1 && policy && syncs==2);
 policy=false;early();assert(!policy && syncs==2);
 for(int size=0;size<128;size++){
  Collision data;for(int i=0;i<size;i++)data.contacts.push_back({i*7,i*11});
  for(int i=0;i<GetCount(&data,NULL);i++){auto a=GetContact(&data,i,NULL);assert(a.normal==data.contacts[i].normal && a.point==data.contacts[i].point);}
  if(size){auto first=GetContact(&data,0,NULL);assert(first.normal==0 && first.point==0);}
  bool raised=false;try{GetContact(&data,size,NULL);}catch(const std::out_of_range&){raised=true;}assert(raised);
 }
}
'''.replace('HEADER',header)
        (tmp/'check.cpp').write_text(cpp)
        subprocess.run([a.cxx,'-std=c++17','-O2',str(tmp/'check.cpp'),'-o',str(tmp/'check.exe')],check=True)
        subprocess.run([str(tmp/'check.exe')],check=True)
    print('PASS: exact autoaim bodies unchanged/idempotent; production RAII restores on nested/early/throw exits; 15 contact readers retain response/order/normal/point operations; contact model 0..127/bounds')

if __name__=='__main__':main()
