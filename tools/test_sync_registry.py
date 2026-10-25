"""Exercise actual registry publication races and Android pthread adapters."""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
THREAD_PRE=r"""
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
typedef HANDLE Worker;
#define WORKER DWORD WINAPI
#define FINISH return 0
static void yield_cpu(void){Sleep(0);}
static Worker spawn(LPTHREAD_START_ROUTINE fn,void *arg){HANDLE h=CreateThread(NULL,0,fn,arg,0,NULL);assert(h);return h;}
static void join(Worker h){assert(WaitForSingleObject(h,10000)==WAIT_OBJECT_0);CloseHandle(h);}
#else
#include <pthread.h>
#include <sched.h>
typedef pthread_t Worker;
#define WORKER void *
#define FINISH return NULL
static void yield_cpu(void){sched_yield();}
static Worker spawn(void *(*fn)(void*),void *arg){pthread_t t;assert(!pthread_create(&t,NULL,fn,arg));return t;}
static void join(Worker t){assert(!pthread_join(t,NULL));}
#endif
static __thread int park_allocation;
static int parked,release_creator,fail_allocation;
static void *test_calloc(size_t a,size_t b){
 if(park_allocation){park_allocation=0;__atomic_store_n(&parked,1,__ATOMIC_RELEASE);while(!__atomic_load_n(&release_creator,__ATOMIC_ACQUIRE))yield_cpu();}
 if(fail_allocation){fail_allocation=0;return NULL;}return calloc(a,b);
}
#define calloc test_calloc
"""
THREAD_MAIN=r"""
#undef calloc
static unsigned values[8][32];static int start;
static WORKER contend(void *arg){
 unsigned id=(unsigned)(uintptr_t)arg;
 while(!__atomic_load_n(&start,__ATOMIC_ACQUIRE))yield_cpu();
 for(unsigned j=0;j<32;j++){
  values[id][j]=0x12340000u+j;
  unsigned *winner=nx_sync_publish((void*)(uintptr_t)(0x100000+j*4),&values[id][j]);
  assert(winner&&*winner==0x12340000u+j);
  assert(nx_sync_lookup((void*)(uintptr_t)(0x100000+j*4))==winner);
 }
 FINISH;
}
static int first_object,second_object,other_object;static void *first_result;
static WORKER stopped_creator(void *unused){
 (void)unused;park_allocation=1;
 first_result=nx_sync_publish((void*)0x700004,&first_object);FINISH;
}
int main(void){
 Worker stopped=spawn(stopped_creator,NULL);
 while(!__atomic_load_n(&parked,__ATOMIC_ACQUIRE))yield_cpu();
 /* Creator is parked inside allocation. Both an unrelated lookup and another
  * initializer of the SAME object must complete before creator can resume. */
 assert(nx_sync_publish((void*)0x700008,&other_object)==&other_object);
 assert(nx_sync_publish((void*)0x700004,&second_object)==&second_object);
 __atomic_store_n(&release_creator,1,__ATOMIC_RELEASE);join(stopped);
 assert(first_result==&second_object);
 Worker workers[8];for(unsigned i=0;i<8;i++)workers[i]=spawn(contend,(void*)(uintptr_t)i);
 __atomic_store_n(&start,1,__ATOMIC_RELEASE);for(int i=0;i<8;i++)join(workers[i]);
 uintptr_t a=0x800004,b=a+4;while(bucket(a)!=bucket(b))b+=4;
 assert(nx_sync_publish((void*)a,&first_object)==&first_object);
 assert(nx_sync_publish((void*)b,&second_object)==&second_object);
 assert(nx_sync_remove((void*)a)==&first_object);
 assert(nx_sync_publish((void*)b,&other_object)==&second_object);
 assert(nx_sync_remove((void*)b)==&second_object&&!nx_sync_lookup((void*)b));
 for(unsigned i=0;i<50000;i++)assert(nx_sync_publish((void*)(uintptr_t)(0x1000000+i*4),&first_object)==&first_object);
 for(unsigned i=0;i<2000;i++){
  assert(nx_sync_publish((void*)a,&other_object)==&other_object);
  assert(nx_sync_remove((void*)a)==&other_object);
 }
 fail_allocation=1;assert(!nx_sync_publish((void*)0x90000004,&first_object));
 assert(!nx_sync_lookup((void*)0x90000004));
 assert(nx_sync_publish((void*)0x90000004,&second_object)==&second_object);
 assert(!nx_sync_lookup(NULL)&&!nx_sync_publish(NULL,&first_object));
 puts("PASS: stopped creator, 8 concurrent publishers, publication visibility, collision/destruction/reuse, 50000 distinct addresses and OOM recovery");
}
"""
ADAPTER_PRE=r"""
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
typedef struct {int held,type;} host_pthread_mutex_t;
typedef struct {int ready;} host_pthread_cond_t;
typedef int host_pthread_mutexattr_t;
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define DIAG_W_MUTEX 1
#define DIAG_W_COND 2
#define DIAG_W_RWLOCK 3
typedef struct {int readers,writer;} RwLock;
static void rwlockInit(RwLock *r){r->readers=r->writer=0;}
static void rwlockReadLock(RwLock *r){assert(!r->writer);r->readers++;}
static void rwlockWriteLock(RwLock *r){assert(!r->readers&&!r->writer);r->writer=1;}
static void rwlockReadUnlock(RwLock *r){assert(r->readers);r->readers--;}
static void rwlockWriteUnlock(RwLock *r){assert(r->writer);r->writer=0;}
static int rwlockIsWriteLockHeldByCurrentThread(RwLock *r){return r->writer;}
static int mutex_live,cond_live;
static void *inject_storage;static host_pthread_mutex_t *inject_winner;
static int host_pthread_mutexattr_init(int *a){*a=0;return 0;}
static int host_pthread_mutexattr_settype(int *a,int t){*a=t;return 0;}
static int host_pthread_mutexattr_destroy(int *a){(void)a;return 0;}
static int host_pthread_mutex_init(host_pthread_mutex_t *m,const int *a){
 m->held=0;m->type=a?*a:0;mutex_live++;
 if(inject_storage){void *s=inject_storage;inject_storage=NULL;assert(nx_sync_publish(s,inject_winner)==inject_winner);}
 return 0;
}
static int host_pthread_mutex_destroy(host_pthread_mutex_t *m){if(m->held)return EBUSY;mutex_live--;return 0;}
static int host_pthread_mutex_trylock(host_pthread_mutex_t *m){if(m->held&&m->type!=1)return EBUSY;m->held++;return 0;}
static int host_pthread_mutex_lock(host_pthread_mutex_t *m){if(m->held&&m->type!=1)return EDEADLK;m->held++;return 0;}
static int host_pthread_mutex_unlock(host_pthread_mutex_t *m){if(!m->held)return EPERM;m->held--;return 0;}
static int host_pthread_cond_init(host_pthread_cond_t *c,const void *a){(void)a;c->ready=1;cond_live++;return 0;}
static int host_pthread_cond_destroy(host_pthread_cond_t *c){assert(c->ready);cond_live--;return 0;}
static int host_pthread_cond_signal(host_pthread_cond_t *c){assert(c->ready);return 0;}
static int host_pthread_cond_broadcast(host_pthread_cond_t *c){assert(c->ready);return 0;}
static int host_pthread_cond_wait(host_pthread_cond_t *c,host_pthread_mutex_t *m){assert(c->ready&&m->held);return 0;}
static int host_pthread_cond_timedwait(host_pthread_cond_t *c,host_pthread_mutex_t *m,const struct timespec *t){assert(c->ready&&m->held&&t);return ETIMEDOUT;}
static void diag_wait_enter(int k,const void *o){assert((k==1||k==2||k==3)&&o);}
static void diag_wait_exit(void){}
static void svcSleepThread(uint64_t n){(void)n;}
"""
ADAPTER_MAIN=r"""
int main(void){
 uint32_t raw[4] __attribute__((aligned(8)))={0xdeadbeef,0,0xabcdef01,0x12345678};
 host_pthread_mutex_t **m=(void*)&raw[1];assert((uintptr_t)m%8==4);
 assert(!host_pthread_mutex_lock_fake(m)&&raw[0]==0xdeadbeef&&raw[2]==0xabcdef01);
 assert(host_pthread_mutex_destroy_fake(m)==EBUSY);
 assert(!host_pthread_mutex_unlock_fake(m)&&!host_pthread_mutex_destroy_fake(m)&&raw[1]==0);
 raw[1]=0x4000;assert(!host_pthread_mutex_lock_fake(m)&&!host_pthread_mutex_lock_fake(m));
 assert(!host_pthread_mutex_unlock_fake(m)&&!host_pthread_mutex_unlock_fake(m));
 assert(!host_pthread_mutex_destroy_fake(m));
 raw[1]=0x8000;assert(!host_pthread_mutex_lock_fake(m)&&host_pthread_mutex_lock_fake(m)==EDEADLK);
 assert(!host_pthread_mutex_unlock_fake(m)&&!host_pthread_mutex_destroy_fake(m));
 host_pthread_cond_t **c=(void*)&raw[2];raw[2]=0;
 assert(!host_pthread_cond_signal_fake(c)&&!host_pthread_cond_broadcast_fake(c));
 assert(!host_pthread_mutex_lock_fake(m)&&!host_pthread_cond_wait_fake(c,m));
 assert(host_pthread_cond_timedwait_fake(c,m,NULL)==ETIMEDOUT);
 assert(!host_pthread_mutex_unlock_fake(m));
 assert(!host_pthread_cond_destroy_fake(c)&&!host_pthread_mutex_destroy_fake(m));
 inject_winner=calloc(1,sizeof(*inject_winner));assert(inject_winner);
 assert(!host_pthread_mutex_init(inject_winner,NULL));inject_storage=m;
 /* Another initializer wins AFTER initial lookup and native allocation. */
 assert(!host_pthread_mutex_init_fake(m,NULL));assert(nx_sync_lookup(m)==inject_winner);
 assert(mutex_live==1);assert(!host_pthread_mutex_destroy_fake(m));
 uint32_t rw[4] __attribute__((aligned(8)))={0xfeedface,0,0xbeefbeef,0};
 void **rw_storage=(void*)&rw[1];
 assert(!host_pthread_rwlock_rdlock_fake(rw_storage)&&!host_pthread_rwlock_unlock_fake(rw_storage));
 assert(!host_pthread_rwlock_wrlock_fake(rw_storage)&&!host_pthread_rwlock_unlock_fake(rw_storage));
 assert(!host_pthread_rwlock_destroy_fake(rw_storage)&&!rw[1]&&rw[0]==0xfeedface&&rw[2]==0xbeefbeef);
 assert(!mutex_live&&!cond_live&&raw[0]==0xdeadbeef&&raw[3]==0x12345678);
 puts("PASS: actual mutex/condition adapters preserve 4-byte storage, recursive/errorcheck flags, busy destruction, waits and losing-initializer cleanup");
}
"""
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    registry=(ROOT/'loader/source/nx_sync_registry.c').read_text()
    source=(ROOT/'loader/source/imports.c').read_text()
    begin=source.index('#define NX_SYNC_LIVE');end=source.index('int pthread_once_fake',begin)
    rwsource=(ROOT/'loader/source/libc_shim.c').read_text()
    rwbegin=rwsource.index('typedef struct { RwLock lock; } FakeRwLock;')
    rwend=rwsource.index('typedef struct { Semaphore sem; } FakeSem;',rwbegin)
    adapters=(source[begin:end]+rwsource[rwbegin:rwend]).replace('pthread_','host_pthread_')
    with tempfile.TemporaryDirectory(prefix='nx-sync-') as tmp:
        p=Path(tmp)
        for name,text in [('registry',THREAD_PRE+registry+THREAD_MAIN),('adapters',registry+ADAPTER_PRE+adapters+ADAPTER_MAIN)]:
            c=p/(name+'.c');exe=p/(name+'.exe');c.write_text(text)
            subprocess.run([args.cc,'-O2','-std=gnu11','-Wall','-Wextra','-pthread','-I',str(ROOT/'loader/source'),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=20)
if __name__=='__main__':main()
