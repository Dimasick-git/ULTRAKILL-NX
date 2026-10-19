"""Exercise production bridge initialization against a SaltySD protocol stub."""
from pathlib import Path
import argparse, subprocess, tempfile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    code=(root/'loader/source/nx_salty.c').read_text()
    code=code[code.index('static SharedMemory memory;'):code.index('void nx_salty_present(')]
    header=(root/'loader/source/nx_salty.h').read_text().replace('#pragma once','')
    stubs=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
typedef unsigned Handle,Result;
typedef struct {int mapped;} SharedMemory;
typedef struct {Handle handle;} Service;
typedef struct {bool in_send_pid; int out_handle_attrs[1];Handle* out_handles;} Params;
static unsigned char area[4096];
static int unavailable,map_fail,connects,dispatches,reservations,closes;
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define Perm_Rw 3
#define SfOutHandleAttr_HipcCopy 1
#define Module_Libnx 345
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define debugPrintf(...) ((void)0)
static Result svcConnectToNamedPort(Handle* h,const char* name) {
 ++connects;assert(!strcmp(name,"SaltySD"));*h=1;return unavailable?123:0;
}
static void serviceCreate(Service* s,Handle h) {s->handle=h;}
static Result dispatch(Service* s,unsigned cmd,Params p) {
 assert(s->handle==1);++dispatches;
 if(cmd==7) {assert(p.in_send_pid && p.out_handle_attrs[0]==1 && p.out_handles);*p.out_handles=2;return 0;}
 assert(cmd==0);return 0;
}
#define serviceDispatch(s,cmd,...) dispatch(s,cmd,(Params){__VA_ARGS__})
static Result reserve(Service* s,unsigned cmd,uint64_t size,uint64_t* offset,Params p) {
 assert(s->handle==1 && cmd==6 && size==310 && p.in_send_pid);
 ++reservations;*offset=0;return 0;
}
#define serviceDispatchInOut(s,cmd,in,out,...) reserve(s,cmd,in,&out,(Params){__VA_ARGS__})
static void shmemLoadRemote(SharedMemory* m,Handle h,unsigned size,int perm) {
 assert(h==2 && size==4096 && perm==3);m->mapped=0;
}
static Result shmemMap(SharedMemory* m) {m->mapped=!map_fail;return map_fail?456:0;}
static void* shmemGetAddr(SharedMemory* m) {assert(m->mapped);return area;}
static void shmemClose(SharedMemory* m) {m->mapped=0;++closes;}
static void serviceClose(Service* s) {s->handle=0;}
'''
    tests=r'''
static void reset(void) {
 block=0;memset(area,0,sizeof(area));unavailable=map_fail=connects=dispatches=reservations=closes=0;
}
int main(void) {
 reset();nx_salty_init();assert(block==(void*)area && block->magic==0x465053 && block->active && block->api==2);
 assert(connects==1 && dispatches==2 && reservations==1 && closes==0);
 reset();unavailable=1;nx_salty_init();assert(!block && !dispatches && !reservations);
 reset();map_fail=1;nx_salty_init();assert(!block && closes==1 && !reservations);
 reset();NxSaltyBlock* other=(void*)area;other->magic=0x465053;other->active=1;other->frame=999;
 nx_salty_init();assert(!block && !reservations && other->frame==999 && closes==1);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'session.c';exe=Path(tmp)/'session.exe'
        src.write_text(header+stubs+code+tests)
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: SaltySD port, PID/copied handle/reservation, absent service, map failure, existing producer preserved')

if __name__=='__main__':main()
