import argparse,re,subprocess,tempfile
from pathlib import Path
from patch_exception_trace import patch_raise
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--cxx',default='g++');a=p.parse_args()
 original='''    NORETURN void Exception::Raise(Il2CppException* ex, MethodInfo* lastManagedFrame)
    {
        PrepareExceptionForThrow(ex, lastManagedFrame);
        throw Il2CppExceptionWrapper(ex);
    }
'''
 changed=patch_raise(original);assert patch_raise(changed)==changed
 native=(ROOT/'loader/source/nx_exception_trace.c').read_text();native=native[native.index('void nxb_raise_trace('):]
 fixture=r'''
#include <cassert>
#include <cstring>
#include <cstddef>
static int samples,prepared;
static void trace_sample(const char*,unsigned index){assert(index<=4);++samples;}
NATIVE
struct Klass{const char* name;};struct Il2CppException{Klass* klass;};struct MethodInfo{};
struct Il2CppExceptionWrapper{Il2CppException* ex;explicit Il2CppExceptionWrapper(Il2CppException* e):ex(e){}};
static bool available=true;
static void* lookup(int,const char* name){assert(!strcmp(name,"nxb_raise_trace"));return available?(void*)&nxb_raise_trace:nullptr;}
#define dlsym lookup
#define RTLD_DEFAULT 0
#define IL2CPP_TARGET_ANDROID 1
#define NORETURN [[noreturn]]
struct Exception{NORETURN static void Raise(Il2CppException*,MethodInfo*);};
static void PrepareExceptionForThrow(Il2CppException*,MethodInfo*){++prepared;}
CHANGED
int main(){
 nxb_raise_trace(nullptr);nxb_raise_trace("Other");assert(!samples);
 Klass k{"ArgumentOutOfRangeException"};Il2CppException ex{&k};
 for(int i=0;i<100;i++){try{Exception::Raise(&ex,nullptr);}catch(Il2CppExceptionWrapper e){assert(e.ex==&ex);}}
 assert(samples==4 && prepared==100);
 try{Exception::Raise(nullptr,nullptr);}catch(Il2CppExceptionWrapper e){assert(!e.ex);}
 assert(samples==4 && prepared==101);
}
'''.replace('NATIVE',native).replace('CHANGED',changed)
 with tempfile.TemporaryDirectory() as tmp:
  src=Path(tmp)/'trace.cpp';exe=Path(tmp)/'trace.exe';src.write_text(fixture)
  subprocess.run([a.cxx,'-std=c++17','-O2',str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 print('PASS: actual pre-unwind callback bounded to 4 range samples; original preparation/throw/object unchanged; idempotent')
if __name__=='__main__':main()
