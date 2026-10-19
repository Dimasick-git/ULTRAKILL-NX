"""Exercise actual close/dup/fstat wrappers against checked native handles."""
from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]

def function(text,name):
    start=re.search(r'^\w[^\n]*\b'+name+r'\([^\n]*\)\s*\{',text,re.M).start()
    brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
    shim=(ROOT/'loader/source/libc_shim.c').read_text(encoding='utf-8')
    unity=(ROOT/'loader/source/unity_imports.c').read_text(encoding='utf-8')
    pre=r'''
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
struct stat {int value;};struct bionic_stat {uint64_t st_ino;};
static int held,closes,duplicates,stat_calls,fail_close,fail_dup,ino_cleared;
static int g_watch_fd=-1;static uint64_t g_fd_ino[32];
#define FD_INO_MAX 32
#define debugPrintf(...) ((void)0)
void nx_fd_lock(int fd){assert(fd==13);assert(!held);held=1;}
void nx_fd_unlock(int fd){assert(fd==13&&held);held=0;}
static int fakefd_is_fake(int fd){return fd==9000;}
static int fakefd_close(int fd){assert(fd==9000&&!held);return 0;}
static void fd_ino_set(int fd,const char *p){assert(fd==13&&held&&!p);ino_cleared++;}
void nx_fd_ino_dup(int from,int to){assert(held&&from==13&&to==14);duplicates++;}
static int mock_close(int fd){assert(fd==13&&held);closes++;if(fail_close){errno=EBADF;return -1;}return 0;}
static int mock_dup(int fd){assert(fd==13&&held);if(fail_dup){errno=EMFILE;return -1;}return 14;}
static int mock_fileno(FILE *f){assert(f==(FILE*)13);return 13;}
static int mock_fclose(FILE *f){assert(f==(FILE*)13&&held);return mock_close(13);}
static int is_fake_file(FILE *f){return f==(FILE*)9000;}
static int mock_fstat(int fd,struct stat *s){assert(fd==13&&held);stat_calls++;s->value=1;return 0;}
static void convert_stat(const struct stat *s,struct bionic_stat *b){assert(held&&s->value==1);b->st_ino=0;}
#define close mock_close
#define dup mock_dup
#define fileno mock_fileno
#define fclose mock_fclose
#define fstat mock_fstat
'''
    code=''.join(function(shim,n) for n in ('close_fake','fclose_fake','fstat_fake'))+function(unity,'z_dup')
    test=r'''
int main(void){
 assert(close_fake(13)==0&&!held&&closes==1&&ino_cleared==1);
 fail_close=1;assert(close_fake(13)==-1&&errno==EBADF&&!held);fail_close=0;
 assert(fclose_fake((FILE*)13)==0&&!held&&closes==3&&ino_cleared==3);
 assert(close_fake(9000)==0&&fclose_fake((FILE*)9000)==0&&!held);
 assert(z_dup(13)==14&&duplicates==1&&!held);
 fail_dup=1;assert(z_dup(13)==-1&&errno==EMFILE&&duplicates==1&&!held);
 struct bionic_stat s;g_fd_ino[13]=123;assert(fstat_fake(13,&s)==0&&s.st_ino==123&&stat_calls==1&&!held);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';p.write_text(pre+code+test)
        subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: real close/fclose/dup/fstat hold the operation lock, error paths unlock, failed dup never aliases the source')
if __name__=='__main__':main()
