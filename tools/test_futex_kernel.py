"""Compile actual futex shim; parked userspace mutexes must be irrelevant."""
from pathlib import Path
import argparse,subprocess,tempfile,struct,json
ROOT=Path(__file__).resolve().parents[1]
PRE=r"""
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned Result;typedef uint64_t u64;typedef int Mutex,CondVar;
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define KERNELRESULT(x) KernelError_##x
enum {KernelError_InvalidAddress=102,KernelError_InvalidMemoryState=106,KernelError_TimedOut=117,KernelError_Cancelled=118,KernelError_InvalidState=125};
enum {ArbitrationType_WaitIfEqual=2,SignalType_Signal=0,DIAG_W_FUTEX=6};
static int waits,wakes,entered,exited,clock_id,count,change_before_wait;
static int64_t last_timeout;static void *last_address;static Result wait_rc,wake_rc;
static int fake_clock_gettime(int id,struct timespec *to){clock_id=id;to->tv_sec=200;to->tv_nsec=500000000;return 0;}
#define clock_gettime fake_clock_gettime
static void mutexLock(Mutex *m){(void)m;exit(71);} /* Models an owner suspended by GC. */
static void mutexUnlock(Mutex *m){(void)m;assert(!"unexpected userspace mutex");}
static Result condvarWaitTimeout(CondVar *c,Mutex *m,uint64_t t){(void)c;(void)m;(void)t;exit(71);}
static void condvarWakeAll(CondVar *c){(void)c;exit(71);}
static void diag_futex_spin(const void *p){(void)p;}
static void diag_wait_enter(int kind,const void *p){assert(kind==DIAG_W_FUTEX&&p);entered++;}
static void diag_wait_exit(void){exited++;}
static Result svcWaitForAddress(void *address,unsigned type,int64_t value,int64_t timeout){
 assert(type==ArbitrationType_WaitIfEqual);waits++;last_address=address;last_timeout=timeout;
 if(change_before_wait){change_before_wait=0;*(int32_t*)address=99;}
 if(*(int32_t*)address!=(int32_t)value)return KernelError_InvalidState;
 return wait_rc;
}
static Result svcSignalToAddress(void *address,unsigned type,int value,int n){
 assert(type==SignalType_Signal&&value==0&&n>0);wakes++;last_address=address;count=n;return wake_rc;
}
"""
MAIN=r"""
int main(void){
 int32_t word=7;struct timespec t={0,1000000};
 assert(futex_impl(&word,FUTEX_WAKE|128,3,NULL)==3&&wakes==1&&count==3);
 assert(futex_impl(&word,FUTEX_WAKE,0,NULL)==0&&wakes==1);
 assert(futex_impl(&word,FUTEX_WAKE_BITSET,1,NULL)==1&&count==INT32_MAX);
 assert(futex_impl(&word,FUTEX_WAIT|128,7,NULL)==0&&last_timeout==16000000&&last_address==&word);
 change_before_wait=1;assert(futex_impl(&word,FUTEX_WAIT,7,NULL)==-1&&errno==EAGAIN);word=7;
 wait_rc=KernelError_TimedOut;
 assert(futex_impl(&word,FUTEX_WAIT,7,NULL)==0);
 assert(futex_impl(&word,FUTEX_WAIT,7,&t)==-1&&errno==ETIMEDOUT&&last_timeout==1000000);
 t.tv_sec=1000;assert(futex_impl(&word,FUTEX_WAIT,7,&t)==0&&last_timeout==16000000);
 t.tv_sec=200;t.tv_nsec=502000000;
 assert(futex_impl(&word,FUTEX_WAIT_BITSET,7,&t)==-1&&errno==ETIMEDOUT&&last_timeout==2000000&&clock_id==CLOCK_MONOTONIC);
 assert(futex_impl(&word,FUTEX_WAIT_BITSET|256,7,&t)==-1&&clock_id==CLOCK_REALTIME);
 t.tv_sec=199;assert(futex_impl(&word,FUTEX_WAIT_BITSET,7,&t)==-1&&last_timeout==0);
 t.tv_sec=INT64_MAX;t.tv_nsec=999999999;assert(futex_impl(&word,FUTEX_WAIT,7,&t)==0&&last_timeout==16000000);
 t.tv_sec=-1;assert(futex_impl(&word,FUTEX_WAIT,7,&t)==-1&&errno==EINVAL);
 t.tv_sec=0;t.tv_nsec=1000000000;assert(futex_impl(&word,FUTEX_WAIT,7,&t)==-1&&errno==EINVAL);
 wait_rc=KernelError_Cancelled;assert(futex_impl(&word,FUTEX_WAIT,7,NULL)==-1&&errno==EINTR);
 wait_rc=KernelError_InvalidAddress;assert(futex_impl(&word,FUTEX_WAIT,7,NULL)==-1&&errno==EFAULT);
 wake_rc=KernelError_InvalidMemoryState;assert(futex_impl(&word,FUTEX_WAKE,1,NULL)==-1&&errno==EFAULT);
 assert(futex_impl(NULL,FUTEX_WAKE,1,NULL)==-1&&errno==EFAULT);
 assert(futex_impl((void*)3,FUTEX_WAIT,1,NULL)==-1&&errno==EINVAL);
 assert(futex_impl(&word,55,1,NULL)==-1&&errno==ENOSYS);
 assert(entered==exited&&waits==entered);
 puts("PASS: wait/wake never take a userspace mutex; kernel compare prevents lost wake; deadline/clock/overflow/error paths and diagnostics");
}
"""
def section(path):
 s=path.read_text();return s[s.index('#define FUTEX_WAIT '):s.index('/* newlib has no <sys/uio.h>')]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');ap.add_argument('--legacy-source',type=Path);a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='nx-futex-') as tmp:
  p=Path(tmp)
  cases=[('kernel',section(ROOT/'loader/source/libc_shim.c'),MAIN,0)]
  if a.legacy_source:cases.append(('legacy',section(a.legacy_source),'int main(void){int32_t v=1;futex_impl(&v,FUTEX_WAKE,1,NULL);return 0;}',71))
  for name,source,test,expected in cases:
   c=p/(name+'.c');exe=p/(name+'.exe');c.write_text(PRE+source+test)
   subprocess.run([a.cc,'-std=gnu11','-O2','-Wno-discarded-qualifiers',str(c),'-o',str(exe)],check=True)
   result=subprocess.run([str(exe)],timeout=10);assert result.returncode==expected,(name,result.returncode)
  if a.legacy_source:print('PASS: exact r39 source fails negative control on the blocked FUTEX_WAKE mutex from console log')
 # The binary is a local build product. CI checks the tracked source policy;
 # local builds additionally check the compiled launcher that enters the NSP.
 policy=json.loads((ROOT/'forwarder/hbl.json').read_text())
 calls=next(x['value'] for x in policy['kernel_capabilities'] if x['type']=='syscalls')
 assert {0x34,0x35}<={int(v,0) for v in calls.values()}
 binary=ROOT/'forwarder/hbl.npdm'
 if binary.is_file():
  data=binary.read_bytes();aci=struct.unpack_from('<I',data,0x70)[0]
  offset,size=struct.unpack_from('<II',data,aci+0x30);allowed=set()
  for at in range(aci+offset,aci+offset+size,4):
   cap=struct.unpack_from('<I',data,at)[0]
   if cap&0x1f==0xf:
    index=cap>>29;mask=(cap>>5)&0xffffff
    allowed.update(index*24+i for i in range(24) if mask&(1<<i))
  assert {0x34,0x35}<=allowed,'Launcher must allow address-arbitration SVCs'
  print('PASS: actual launcher NPDM permits SVC 0x34 and 0x35')
 else:print('PASS: source launcher policy permits SVC 0x34 and 0x35 (compile NPDM before packaging)')
if __name__=='__main__':main()
