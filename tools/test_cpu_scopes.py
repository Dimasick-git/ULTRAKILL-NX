from pathlib import Path
import argparse,re,subprocess,tempfile
from patch_cpu_scopes import TARGETS,HEADER,patch
from patch_nx_game_fixes import SCOPES
ROOT=Path(__file__).resolve().parents[1]
def main():
    assert 'cntpct_el0' in HEADER and 'cntvct_el0' not in HEADER
    ap=argparse.ArgumentParser();ap.add_argument('--cxx',default='g++')
    ap.add_argument('--cxx-arg',action='append',default=[]);a=ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp);p=root/'Assembly-CSharp.cpp'
        text='\ufeff#include "il2cpp.h"\n'+'\n'.join(f'IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void {name}_mABC ()\n{{\n if (true) return;\n}}' for name in TARGETS)
        p.write_text(text,encoding='utf-8');assert len(patch(root))==len(TARGETS)
        once=p.read_bytes();patch(root);assert p.read_bytes()==once
        assert b'\xef\xbb\xbf' not in once and once.count(b'NX_CPU_SCOPE')==len(TARGETS)
        scope='\n\tNxPortScope nx_scope(0); // NX_CPU_SCOPE'
        shifted=once.decode().replace(scope,'\n\tif (skip) return;'+scope+scope,1)
        p.write_text(shifted,encoding='utf-8');patch(root)
        repaired=p.read_bytes()
        assert repaired.count(b'NX_CPU_SCOPE')==len(TARGETS) and b'if (skip) return;' in repaired
        patch(root);assert p.read_bytes()==repaired
        p.write_text(text.replace(TARGETS[0],'missing'),encoding='utf-8')
        try:patch(root)
        except AssertionError:pass
        else:raise AssertionError('Missing generated method accepted')
        assert p.read_text(encoding='utf-8')==text.replace(TARGETS[0],'missing')
        pre=r'''
#include <cassert>
#include <cstring>
static unsigned count,ids[8];static unsigned long long times[8],clock_value;
static void record(unsigned id,unsigned long long value){ids[count]=id;times[count++]=value;}
static int profile(const char *name){assert(!strcmp(name,"engine_profiler"));return PROFILE_ENABLED;}
static void *lookup(void *,const char *name){if(!strcmp(name,"nxb_config_int"))return (void*)&profile;assert(!strcmp(name,"nxb_scope_record"));return (void*)&record;}
#define NX_SCOPE_TICKS() (++clock_value)
#define RTLD_DEFAULT nullptr
#define dlsym lookup
'''
        helper=HEADER.replace('#include <dlfcn.h>','').replace('#pragma once','')
        test=r'''
static void early(){NxPortScope outer(0);{NxPortScope inner(1);}return;}
static void unwind(){NxPortScope scope(2);throw 7;}
int main(){early();assert(count==2&&ids[0]==1&&times[0]==1&&ids[1]==0&&times[1]==3);
try{unwind();}catch(int value){assert(value==7);}assert(count==3&&ids[2]==2&&times[2]==1);}
'''
        source=root/'scope.cpp';exe=root/'scope.exe'
        for enabled in (0,1):
            body=test if enabled else 'int main(){early();try{unwind();}catch(int){}assert(count==0 && clock_value==0);}'
            # Both helpers must exist when diagnostics are disabled as well.
            if not enabled:body=test[:test.index('int main()')]+body
            source.write_text('#define PROFILE_ENABLED '+str(enabled)+'\n'+pre+helper+body)
            subprocess.run([a.cxx,*a.cxx_arg,'-std=c++17','-O2','-UNDEBUG','-Wall',str(source),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
    native=(ROOT/'loader/source/nx_cpu_scopes.c').read_text(encoding='utf-8')
    labels=re.search(r'names\[\]\s*=\{(.*?)\};',native,re.S).group(1)
    extra=[name for name,sid in sorted(SCOPES.items(),key=lambda item:item[1])]
    assert sorted(SCOPES.values())==list(range(len(TARGETS),len(TARGETS)+len(extra)))
    assert re.findall(r'"([^"]+)"',labels)==list(TARGETS)+extra
    print('PASS: exact methods, BOM handling, idempotence, no partial patch, nested/early/exception exits retain semantics; disabled scopes read no timer and record nothing')
if __name__=='__main__':main()
