"""Run actual nx_cpu.c with a deterministic kernel adapter on the host."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
typedef uint64_t u64; typedef uint32_t u32; typedef int32_t s32;
typedef unsigned Handle; typedef unsigned Result;
#define InfoType_CoreMask 0
#define CUR_PROCESS_HANDLE 1
#define CUR_THREAD_HANDLE 2
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
static u64 permitted=7, thread_mask=7; static int fail_info,fail_thread,fail_set;
static Result svcGetInfo(u64 *v,int type,Handle h,int sub){*v=permitted;return fail_info;}
static Result svcGetThreadCoreMask(s32 *p,u64 *v,Handle h){*p=0;*v=thread_mask;return fail_thread;}
static Result svcSetThreadCoreMask(Handle h,s32 p,u32 v){if(fail_set)return 1;thread_mask=v;return 0;}
static unsigned svcGetCurrentProcessorNumber(void){return 1;}
static Handle diag_find_tid_handle(int pid){return pid==77?3:0;}
#define debugPrintf(...) ((void)0)
'''
MAIN=r'''
int main(void){
 uint64_t mask[2]={~0ull,~0ull};
 nx_cpu_init();assert(nx_cpu_count()==3&&nx_cpu_allowed_mask()==7);
 nx_cpu_thread_named(3,"UnityGfxDeviceWorker");assert(thread_mask==6);
 nx_cpu_thread_named(3,"Job.Worker 1");assert(thread_mask==6);
 thread_mask=1;nx_cpu_thread_named(3,"UnityMain");assert(thread_mask==1);
 nx_cpu_thread_named(3,"FMOD mixer thread");assert(thread_mask==1);
 thread_mask=7;
 assert(!strcmp(nx_cpu_list(),"0,1,2\n"));assert(!strstr(nx_cpu_info(),"processor\t: 3"));
 assert(nx_sched_getaffinity(0,sizeof(mask),mask)==0&&mask[0]==7&&mask[1]==0);
 assert(nx_sched_getaffinity(77,sizeof(mask),mask)==0);
 assert(nx_sched_getaffinity(88,sizeof(mask),mask)==-1&&errno==ESRCH);
 assert(nx_sched_getaffinity(0,4,mask)==-1&&errno==EINVAL);
 assert(nx_sched_getaffinity(0,sizeof(mask),NULL)==-1&&errno==EFAULT);
 mask[0]=8;assert(nx_sched_setaffinity(0,sizeof(mask),mask)==-1&&errno==EINVAL);
 mask[0]=0xf;assert(nx_sched_setaffinity(0,sizeof(mask),mask)==0&&thread_mask==7);
 mask[0]=4;assert(nx_sched_setaffinity(77,sizeof(mask),mask)==0&&thread_mask==4);
 fail_set=1;assert(nx_sched_setaffinity(0,sizeof(mask),mask)==-1&&errno==EPERM);fail_set=0;
 fail_thread=1;assert(nx_sched_getaffinity(0,sizeof(mask),mask)==-1&&errno==ESRCH);fail_thread=0;
 permitted=15;thread_mask=15;nx_cpu_init();assert(nx_cpu_count()==4&&nx_cpu_allowed_mask()==15);
 assert(strstr(nx_cpu_info(),"processor\t: 3"));mask[0]=8;
 assert(nx_sched_setaffinity(0,sizeof(mask),mask)==0&&thread_mask==8);
 permitted=5;nx_cpu_init();assert(nx_cpu_count()==2&&!strcmp(nx_cpu_list(),"0,2\n"));
 nx_cpu_thread_named(3,"Job.Worker 0");assert(thread_mask==4);
 permitted=1;nx_cpu_init();thread_mask=1;nx_cpu_thread_named(3,"Job.Worker 0");assert(thread_mask==1);
 fail_info=1;thread_mask=2;nx_cpu_init();assert(nx_cpu_count()==1&&nx_cpu_allowed_mask()==2);
 fail_thread=1;nx_cpu_init();assert(nx_cpu_allowed_mask()==2);
 puts("PASS: real affinity adapter, allowed cores, main-core isolation, sparse/single-core fallback and errors");
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
 src=(ROOT/'loader/source/nx_cpu.c').read_text()
 src='\n'.join(l for l in src.splitlines() if not l.startswith('#include'))
 with tempfile.TemporaryDirectory(prefix='nx-cpu-') as temp:
  p=Path(temp);(p/'test.c').write_text(PRE+src+MAIN)
  subprocess.run([args.cc,'-O2','-std=gnu11',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
  subprocess.run([str(p/'test.exe')],check=True,timeout=15)
if __name__=='__main__':main()
