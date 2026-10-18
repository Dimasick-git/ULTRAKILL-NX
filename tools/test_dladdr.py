"""Test the actual image lookup adapter used by IL2CPP native stack traces."""
import argparse,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef struct {char name[64];void *load_virtbase;size_t load_size;} so_module;
static so_module modules[]={{"libunity.so",(void*)0x10000,0x1000},{"libil2cpp.so",(void*)0x20000,0x2000}};
static so_module *so_find_module_by_addr(const void *addr){
 uintptr_t a=(uintptr_t)addr;
 for(unsigned i=0;i<2;++i)if(a>=(uintptr_t)modules[i].load_virtbase&&a-(uintptr_t)modules[i].load_virtbase<modules[i].load_size)return &modules[i];
 return NULL;
}
'''
MAIN=r'''
int main(void){
 NxDlInfo i;memset(&i,0xff,sizeof(i));
 assert(nx_dladdr((void*)0x10000,&i)==1&&!strcmp(i.dli_fname,"libunity.so")&&i.dli_fbase==(void*)0x10000);
 assert(!i.dli_sname&&!i.dli_saddr);
 assert(nx_dladdr((void*)0x21fff,&i)==1&&!strcmp(i.dli_fname,"libil2cpp.so")&&i.dli_fbase==(void*)0x20000);
 assert(nx_dladdr((void*)0x22000,&i)==0&&!i.dli_fname&&!i.dli_fbase);
 assert(nx_dladdr(NULL,&i)==0&&!i.dli_sname&&!i.dli_saddr);
 assert(nx_dladdr((void*)0x10000,NULL)==0);
 puts("PASS: real dladdr adapter resolves correct image/base, stripped symbols, boundaries, unknown and null addresses");
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
 clean=lambda text:'\n'.join(l for l in text.splitlines() if not l.startswith('#include') and not l.startswith('#pragma'))
 src=PRE+clean((ROOT/'loader/source/nx_dladdr.h').read_text())+clean((ROOT/'loader/source/nx_dladdr.c').read_text())+MAIN
 with tempfile.TemporaryDirectory(prefix='nx-dladdr-') as temp:
  p=Path(temp);(p/'test.c').write_text(src)
  subprocess.run([args.cc,'-O2','-std=gnu11',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
  subprocess.run([str(p/'test.exe')],check=True,timeout=15)
if __name__=='__main__':main()
