"""Exercise the production FastLoad lifecycle with a controlled Horizon stub."""
from pathlib import Path
import argparse,re,subprocess,tempfile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    code=(root/'loader/source/nx_loading.c').read_text()
    code=re.sub(r'^#include[^\n]+\n','',code,flags=re.M)
    pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint64_t u64;
typedef unsigned Result;
typedef int Thread,Mutex,AppletHookCookie,AppletHookType;
enum {ApmCpuBoostMode_Normal=0,ApmCpuBoostMode_FastLoad=1,AppletFocusState_InFocus=1,AppletHookType_OnFocusState=0};
static struct {int loading_boost;} config={1};
static uint64_t now;
static int focused=1,start_calls,normal_calls,boost_fail,normal_fail,create_fail,starts;
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
#define debugPrintf(...) ((void)0)
static uint64_t armGetSystemTick(void){return now;}
static int appletGetFocusState(void){return focused;}
static Result appletSetCpuBoostMode(int mode){if(mode==1){++start_calls;return boost_fail;}++normal_calls;return normal_fail;}
static void mutexInit(Mutex* p){*p=0;}
static void mutexLock(Mutex* p){assert(!*p);*p=1;}
static void mutexUnlock(Mutex* p){assert(*p);*p=0;}
static Result threadCreate(Thread* t,void(*fn)(void*),void* a,void* stack,size_t n,int prio,int core){(void)t;(void)fn;(void)a;(void)stack;assert(n==0x4000 && prio==44 && core==-2);return create_fail;}
static Result threadStart(Thread* t){(void)t;++starts;return 0;}
static void threadClose(Thread* t){(void)t;}
static void threadWaitForExit(Thread* t){(void)t;}
static void appletHook(AppletHookCookie* c,void(*fn)(int,void*),void* a){(void)c;(void)fn;(void)a;}
static void appletUnhook(AppletHookCookie* c){(void)c;}
static void atexit_stub(void(*fn)(void)){(void)fn;}
#define atexit atexit_stub
static void svcSleepThread(uint64_t ns);
'''
    test=r'''
static void svcSleepThread(uint64_t ns){assert(ns==50000000);running=false;}
int main(void){
 nx_loading_init();assert(ready && active && start_calls==1 && starts==1);
 nxb_loading(1);assert(start_calls==1); // repeated notification does not restart timer
 nxb_loading(0);assert(!active && normal_calls==1);
 nxb_loading(1);now=deadline;running=true;timer(NULL);assert(!active && normal_calls==2);
 nxb_loading(1);focused=2;focus_changed(0,NULL);assert(!active && normal_calls==3);
 nxb_loading(1);assert(!active); // HOME/menu must not start boost
 focused=1;boost_fail=1;nxb_loading(1);assert(!active);
 boost_fail=0;nxb_loading(1);assert(active);
 normal_fail=1;nxb_loading(0);assert(active); // failure is not reported as restoration
 int restores=normal_calls;running=true;timer(NULL);assert(active && normal_calls==restores+1);
 normal_fail=0;running=true;timer(NULL);assert(!active); // retry while still in focus, without waiting 60s
 finish();assert(!ready);
 config.loading_boost=0;nx_loading_init();assert(!ready);
 config.loading_boost=1;create_fail=1;nx_loading_init();assert(!ready && !running);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'loading.c';exe=Path(tmp)/'loading.exe';src.write_text(pre+code+test)
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: real FastLoad begin/end, bounded timeout, focus/HOME restoration, rejected requests and timer failure')

if __name__=='__main__':main()
