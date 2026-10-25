"""Ordinary log calls enqueue without file writes; critical logs drain safely."""
from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
    code=(ROOT/'loader/source/nx_log_async.c').read_text()
    code=re.sub(r'^#include <switch.h>\n','',code,flags=re.M)
    code=re.sub(r'^#include "(?:config|nx_gameroot).h"\n','',code,flags=re.M)
    pre=r'''
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#define DEBUG_LOG 1
typedef int Mutex;typedef int Thread;typedef unsigned Result;
#define R_SUCCEEDED(r) ((r)==0)
static int disk_writes,flushes,fail_thread;
static void mutexLock(Mutex *m){assert(!*m);*m=1;}
static void mutexUnlock(Mutex *m){assert(*m);*m=0;}
static Result threadCreate(Thread *t,void(*f)(void*),void*a,void*s,size_t n,int p,int c){(void)t;(void)f;(void)a;(void)s;(void)n;(void)p;(void)c;return fail_thread;}
static Result threadStart(Thread *t){(void)t;return 0;}
static void threadClose(Thread *t){(void)t;}
static void svcSleepThread(int64_t n){(void)n;}
static uint64_t armGetSystemTick(void){static uint64_t n;return ++n;}
static uint64_t armNsToTicks(uint64_t n){return n;}
static const char *game_log_path(void){return "debug.log";}
static FILE *mock_open(const char*p,const char*m){(void)p;(void)m;return tmpfile();}
static size_t mock_write(const void*p,size_t s,size_t n,FILE*f){disk_writes++;return fwrite(p,s,n,f);}
static int mock_flush(FILE*f){flushes++;return fflush(f);}
#define fopen mock_open
#define fwrite mock_write
#define fflush mock_flush
'''
    test=r'''
static void log_line(const char *s,...){va_list a;va_start(a,s);nx_log_printf(s,a);va_end(a);}
int main(void){
 log_line("first %d\n",7);log_line("second\n");assert(asynchronous&&disk_writes==0&&flushes==0);
 nx_log_flush();assert(disk_writes==1&&flushes==1&&queue.size==0);
 rewind(output);char text[64]={0};fread(text,1,sizeof text-1,output);assert(!strcmp(text,"first 7\nsecond\n"));
 log_line("[nullcheck] failure\n");assert(disk_writes==2&&flushes==2&&queue.size==0);
 /* Model a mutator parked inside normal logging: collector diagnostics must
  * neither acquire its mutex nor touch FILE/SD until every mutator resumes. */
 mutexLock(&queue_lock);nx_log_gc_enter();
 log_line("fatal collector %d\n",19);
 nx_log_flush(); /* An explicit critical drain is deferred during STW too. */
 assert(disk_writes==2&&flushes==2&&gc_queue.size>0);
 mutexUnlock(&queue_lock);nx_log_gc_leave();
 assert(gc_queue.size==0&&queue.size>0&&disk_writes==2);
 nx_log_flush();disk_writes=2;flushes=2;
 memset(queue.bytes,'x',sizeof queue.bytes);queue.size=sizeof queue.bytes;
 log_line("ordinary overflow\n");assert(queue.dropped==1&&disk_writes==2);
 log_line("fatal must survive %d\n",42);assert(queue.size==0&&queue.dropped==0&&disk_writes==4);
 fseek(output,-22,SEEK_END);memset(text,0,sizeof text);fread(text,1,sizeof text-1,output);assert(strstr(text,"must survive 42\n"));
 fclose(output);output=NULL;initialized=asynchronous=0;fail_thread=1;disk_writes=flushes=0;
 log_line("fallback\n");assert(!asynchronous&&disk_writes==1&&flushes==1);
 log_line("buffered fallback\n");assert(disk_writes==2&&flushes==1);nx_log_flush();assert(flushes==2);fclose(output);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';p.write_text(pre+code+test)
        subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra','-I',str(ROOT/'loader/source'),str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: ordinary calls do no disk IO, ordered background drain, critical overflow survives, failed worker falls back')
if __name__=='__main__':main()
