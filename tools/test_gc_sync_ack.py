"""Run actual patched Boehm suspension/restart/barrier code with hostile POSIX ACKs."""
from pathlib import Path
import argparse,re,subprocess,tempfile
from patch_gc_horizon import patch
PRE=r"""
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#define UNITY_RETRY_SIGNALS 1
#define STATIC static
#define THREAD_TABLE_SZ 4
#define FINISHED 1
#define THREAD_EQUAL(a,b) ((a)==(b))
#define THREAD_SYSTEM_ID(p) ((p)->id)
#define GC_ASSERT(x) assert(x)
#define ABORT(x) do{fprintf(stderr,"%s\n",x);exit(72);}while(0)
#define ABORT_ARG1(a,b,c) ABORT(a)
#define GC_COND_LOG_PRINTF(...) ((void)0)
#define WARN(...) ((void)0)
typedef uintptr_t word,AO_t,host_pthread_t;
#define AO_load(p) (*(p))
#define AO_store(p,v) (*(p)=(v))
#define AO_store_release(p,v) (*(p)=(v))
typedef struct Node {host_pthread_t id;int flags,thread_blocked;struct Node *next;struct {uintptr_t stack_ptr;AO_t last_stop_count;}stop_info;} *GC_thread;
static GC_thread GC_threads[THREAD_TABLE_SZ];
static int nx_gc_bridge_state=1,GC_retry_signals=1,GC_suspend_ack_sem,GC_sig_suspend=10,GC_sig_thr_restart=11;
static AO_t GC_stop_count=2;
static int paused[8],native_pauses,native_resumes,posix_signals,sem_calls,acks;
static host_pthread_t host_pthread_self(void){return 1;}
static int pause_thread(uintptr_t id,uintptr_t *sp){assert(!paused[id]);paused[id]=1;*sp=0x1000+id*0x100;native_pauses++;return 0;}
static int resume_thread(uintptr_t id){assert(paused[id]);paused[id]=0;native_resumes++;return 0;}
static int (*nx_gc_pause_fn)(uintptr_t,uintptr_t*)=pause_thread;
static int (*nx_gc_resume_fn)(uintptr_t)=resume_thread;
static void (*GC_on_thread_event)(int,void*);
#define GC_EVENT_THREAD_SUSPENDED 1
#define GC_EVENT_THREAD_UNSUSPENDED 2
static int raise_signal(GC_thread p,int sig){(void)p;(void)sig;posix_signals++;acks++;return 0;}
#define RAISE_SIGNAL(p,s) raise_signal(p,s)
static void no_native_ack(void){if(nx_gc_bridge_state==1)exit(71);sem_calls++;}
static int sem_wait(int *s){(void)s;no_native_ack();assert(acks>0);acks--;return 0;}
static int sem_timedwait(int *s,const struct timespec *t){(void)t;return sem_wait(s);}
static int sem_getvalue(int *s,int *value){(void)s;no_native_ack();*value=acks;return 0;}
static int usleep(unsigned long n){(void)n;assert(!"unexpected signal retry delay");return 0;}
"""
MAIN=r"""
int main(void){
 struct Node nodes[5]={0};for(int i=0;i<5;i++){nodes[i].id=i+1;nodes[i].next=i<4?&nodes[i+1]:NULL;}
 nodes[3].flags=FINISHED;nodes[4].thread_blocked=1;GC_threads[0]=nodes;
 for(int retry=0;retry<2;retry++){
  GC_retry_signals=retry;GC_stop_count+=2;
  assert(GC_suspend_all()==2&&paused[2]&&paused[3]&&!paused[1]&&!paused[4]&&!paused[5]);
  assert(nodes[1].stop_info.stack_ptr==0x1200&&nodes[2].stop_info.last_stop_count==GC_stop_count);
  suspend_restart_barrier(2);suspend_restart_barrier_retry(2,GC_suspend_all);
  assert(resend_lost_signals(2,GC_suspend_all)==2);
  assert(!sem_calls&&!posix_signals);
  assert(GC_restart_all()==2&&!paused[2]&&!paused[3]);
  suspend_restart_barrier_retry(2,GC_restart_all);assert(!sem_calls);
  if(retry)assert(GC_restart_all()==0);
 }
 assert(native_pauses==4&&native_resumes==4);
 nx_gc_bridge_state=-1;GC_retry_signals=0;
 assert(GC_suspend_all()==2&&posix_signals==2&&acks==2);
 suspend_restart_barrier(2);assert(!acks);
 assert(GC_restart_all()==2&&posix_signals==4&&acks==2);
 suspend_restart_barrier_retry(2,GC_restart_all);assert(!acks&&sem_calls>=4);
 puts("PASS: actual Boehm suspend/restart callbacks and all ACK barriers; synchronous Horizon never touches POSIX semaphores, Android retains signal ACKs");
}
"""
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');ap.add_argument('--source',required=True,type=Path);a=ap.parse_args()
 text=a.source.read_text();assert 'NX_GC_SYNCHRONOUS_ACK' in text
 with tempfile.TemporaryDirectory(prefix='nx-gc-ack-') as tmp:
  root=Path(tmp);p=root/'pthread_stop_world.c'
  pristine=a.source.with_suffix(a.source.suffix+'.pre-nx-gc')
  p.write_bytes(pristine.read_bytes());patch(p);assert p.read_text()==text
  once=p.read_bytes();patch(p);assert p.read_bytes()==once
  begin=text.index('static void suspend_restart_barrier(int n_live_threads)')
  helpers=text[begin:text.index('STATIC void GC_restart_handler',begin)]
  begin=text.index('STATIC int GC_suspend_all(void)')
  suspend=text[begin:text.index('GC_INNER void GC_stop_world(void)',begin)]
  begin=text.index('  STATIC int GC_restart_all(void)')
  restart=text[begin:text.index('#endif /* !NACL */',begin)]
  code=PRE+(helpers+suspend+restart).replace('pthread_t','host_pthread_t').replace('pthread_self','host_pthread_self')+MAIN
  for name,content in [('current',code),('missing_guard',code.replace('if (nx_gc_bridge_state == 1) return;',''))]:
   c=root/(name+'.c');exe=root/(name+'.exe');c.write_text(content)
   subprocess.run([a.cc,'-std=gnu11','-O2','-Wall',str(c),'-o',str(exe)],check=True)
   result=subprocess.run([str(exe)],timeout=10)
   assert result.returncode==(0 if name=='current' else 71), (name,result.returncode)
  print('PASS: negative control rejects the old native signal-ACK path; fresh patch and upgrade are idempotent')
if __name__=='__main__':main()
