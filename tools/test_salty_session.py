"""Exercise production bridge initialization against a SaltySD protocol stub."""
from pathlib import Path
import argparse, subprocess, tempfile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    code=(root/'loader/source/nx_salty.c').read_text()
    code=code[code.index('static SharedMemory memory;'):]
    header=(root/'loader/source/nx_salty.h').read_text().replace('#pragma once','')
    stubs=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
typedef unsigned Handle,Result;
typedef uint64_t u64;
typedef int64_t s64;
typedef struct {int mapped;} SharedMemory;
typedef struct {Handle handle;} Service;
typedef struct {bool in_send_pid; int out_handle_attrs[1];Handle* out_handles;} Params;
static unsigned char area[4096];
static int unavailable,map_fail,connects,dispatches,reservations,closes;
static int limit_sets,resource_closes,deny_set;
static int64_t session_limit,session_current;
static uint64_t clock_now;
#define InfoType_ResourceLimit 1
#define INVALID_HANDLE 0
#define LimitableResource_Sessions 4
#define AppletOperationMode_Console 1
static uint64_t armGetSystemTick(void){return clock_now;}
static int appletGetOperationMode(void){return 0;}
static uint64_t armTicksToNs(uint64_t t){return t*1000000000/19200000;}
static Result svcGetInfo(uint64_t* out,int type,Handle h,int n){assert(type==1 && h==0 && n==0);*out=42;return 0;}
static Result svcGetResourceLimitLimitValue(int64_t* out,Handle h,int r){assert(h==42 && r==4);*out=session_limit;return 0;}
static Result svcGetResourceLimitCurrentValue(int64_t* out,Handle h,int r){assert(h==42 && r==4);*out=session_current;return 0;}
static Result svcSetResourceLimitLimitValue(Handle h,int r,int64_t v){assert(h==42 && r==4);++limit_sets;if(deny_set)return 99;session_limit=v;unavailable=0;return 0;}
static void svcCloseHandle(Handle h){assert(h==42);++resource_closes;}
static void svcSleepThread(uint64_t ns){clock_now+=ns*19200000/1000000000;}
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define Perm_Rw 3
#define SfOutHandleAttr_HipcCopy 1
#define Module_Libnx 345
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define debugPrintf(...) ((void)0)
static Result svcConnectToNamedPort(Handle* h,const char* name) {
 ++connects;assert(!strcmp(name,"SaltySD"));*h=1;return unavailable;
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
 enabled=foreign_producer=limit_checked=false;attempts=0;retry_at=0;
 session_limit=32;session_current=3;limit_sets=resource_closes=deny_set=0;clock_now=1;
}
int main(void) {
 reset();nx_salty_init();assert(block==(void*)area && block->magic==0x465053 && block->active && block->api==2);
 assert(connects==1 && dispatches==2 && reservations==1 && closes==0);
 reset();unavailable=1;nx_salty_init();assert(!block && !dispatches && !reservations);
 reset();map_fail=1;nx_salty_init();assert(!block && closes==1 && !reservations);
 reset();NxSaltyBlock* other=(void*)area;other->magic=0x465053;other->active=1;other->frame=999;
 nx_salty_init();assert(!block && !reservations && other->frame==999 && closes==1);
 reset();unavailable=0x10801;session_limit=session_current=0;
 nx_salty_init();assert(block && connects==2 && limit_sets==1 && session_limit==8 && resource_closes==1);
 reset();unavailable=0x10801;nx_salty_init();assert(!block && !limit_sets && resource_closes==1);
 unavailable=0;clock_now=retry_at-1;nx_salty_present(clock_now);assert(connects==1);
 ++clock_now;nx_salty_present(clock_now);assert(block && connects==2);
 reset();unavailable=0x10801;session_limit=session_current=0;deny_set=1;
 nx_salty_init();assert(!block && limit_sets==1 && resource_closes==1);
 for(int i=0;i<50;i++){clock_now+=19200000;nx_salty_present(clock_now);}
 assert(connects==30 && attempts==30 && limit_sets==1 && !block);
 reset();nx_salty_present(999999999);assert(!connects); // saltynx 0 leaves bridge disabled
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'session.c';exe=Path(tmp)/'session.exe'
        src.write_text(header+stubs+code+tests)
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: SaltySD protocol, bounded exhausted-session repair, deferred retry/timeout, failure cleanup and foreign producer')

if __name__=='__main__':main()
