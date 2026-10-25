from pathlib import Path
import argparse,subprocess,tempfile
from patch_bounds_site import patch,OLD,NEW

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cxx',default='g++');a=ap.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp);h=root/'libil2cpp/codegen/il2cpp-codegen-il2cpp.h';c=root/'libil2cpp/vm/Exception.cpp'
        h.parent.mkdir(parents=True);c.parent.mkdir(parents=True)
        h.write_text(OLD);c.write_text('#include <dlfcn.h>\nnamespace il2cpp\n{\nnamespace vm\n{\n}}')
        patch(root);snapshot=(h.read_bytes(),c.read_bytes());patch(root);assert snapshot==(h.read_bytes(),c.read_bytes())
        cpp=root/'bounds.cpp';exe=root/'bounds.exe'
        cpp.write_text('''#include <cstdint>
#include <cassert>
int reports=0,throws=0; uint32_t reported_index,reported_length;
extern "C" void nx_il2cpp_bounds_site(const char* file,int line,uint32_t i,uint32_t n) {
 assert(file && line>0);++reports;reported_index=i;reported_length=n;
}
[[noreturn]] void il2cpp_codegen_raise_index_out_of_range_exception(){++throws;throw 7;}
'''+NEW+'''
int main(){
 for(int i=0;i<4;i++) IL2CPP_ARRAY_BOUNDS_CHECK(i,4);
 assert(reports==0 && throws==0);
 int i=4,n=4;try {IL2CPP_ARRAY_BOUNDS_CHECK(i++,n++);assert(false);}catch(int value){assert(value==7);}
 assert(i==5 && n==5 && reports==1 && throws==1 && reported_index==4 && reported_length==4);
 try {IL2CPP_ARRAY_BOUNDS_CHECK(-1,4);assert(false);}catch(int){}
 assert(reported_index==UINT32_MAX && reported_length==4 && reports==2 && throws==2);
 try {IL2CPP_ARRAY_BOUNDS_CHECK(0,0);assert(false);}catch(int){}
 assert(reports==3 && throws==3);
}
''')
        subprocess.run([a.cxx,'-std=c++11',str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
        h.write_text('unexpected runtime');snapshot=(h.read_bytes(),c.read_bytes())
        try:patch(root);raise RuntimeError('unexpected source accepted')
        except AssertionError:pass
        assert snapshot==(h.read_bytes(),c.read_bytes())
    print('PASS: valid accesses silent; upper/negative/empty bounds retain throws; arguments evaluated once; idempotent and atomic validation')
if __name__=='__main__':main()
