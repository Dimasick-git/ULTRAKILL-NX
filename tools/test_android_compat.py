"""Check data-symbol ABI, explicit unsupported paths and real libc adapters."""
from pathlib import Path
import argparse,json,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
    code=(ROOT/'loader/source/nx_android_compat.c').read_text(encoding='utf-8')
    code=re.sub(r'^#include "[^\n]+"\n','',code,flags=re.M)
    pre=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
typedef struct {const char *symbol;uintptr_t func;} DynLibFunction;
static int held,writes,mode;
static char output[64];static size_t used;
static ssize_t mock_write(int fd,const void *buf,size_t n){
 assert(fd==13&&held);writes++;
 if(mode==2&&writes==2){errno=EIO;return -1;}
 if(mode==1)n=1;
 memcpy(output+used,buf,n);used+=n;return n;
}
void nx_fd_lock(int fd){assert(fd==13&&!held);held=1;}
void nx_fd_unlock(int fd){assert(fd==13&&held);held=0;}
int fakefd_is_fake(int fd){return fd==9000;}
long fakefd_write(int fd,const void *p,size_t n){assert(fd==9000&&!held);memcpy(output+used,p,n);used+=n;return n;}
int __android_log_write(int p,const char *tag,const char *text){(void)p;(void)tag;(void)text;return 0;}
int ALooper_pollOnce(int t,int *fd,int *events,void **data){(void)t;(void)fd;(void)events;(void)data;return -3;}
#define debugPrintf(...) ((void)0)
#define write mock_write
'''.replace('#include <assert.h>','#include <assert.h>\n#include <errno.h>')
    test=r'''
int main(void){
 assert(sizeof(CodecInfo)==24&&sizeof(HardwareDesc)==40);
 assert(codec_new("video/avc")==NULL);
 size_t size=123;assert(codec_buffer(NULL,0,&size)==NULL&&size==0);
 void *object=(void*)1;assert(reader_new(1,1,1,0,1,&object)==UNSUPPORTED&&object==NULL);
 CodecInfo info;memset(&info,255,sizeof info);assert(codec_output(NULL,&info,0)==UNSUPPORTED&&info.flags==0&&info.timestamp==0);
 const char *value=(const char*)1;assert(!format_string(NULL,"mime",&value)&&value==NULL);
 int64_t timestamp=123;assert(image_time(NULL,&timestamp)==UNSUPPORTED&&timestamp==0);
 assert(!extractor_advance(NULL)&&extractor_time(NULL)==-1&&extractor_count(NULL)==0);
 assert(!strcmp(case_find("Switch NX","nX"),"NX")&&case_find("abc","z")==NULL);
 assert(nearbyintf(2.5f)==2&&nearbyintf(3.5f)==4);
 assert(no_execl("/bin/a","a",NULL)==-1&&errno==ENOSYS);
 CompatIovec parts[]={{"abc",3},{"de",2}};
 assert(vector_write(13,parts,2)==5&&!held&&used==5&&!memcmp(output,"abcde",5));
 used=writes=0;mode=1;assert(vector_write(13,parts,2)==1&&!held&&used==1&&writes==1);
 used=writes=0;mode=2;assert(vector_write(13,parts,2)==3&&!held&&used==3&&errno==EIO);
 used=0;assert(vector_write(9000,parts,2)==5&&!held&&used==5);
 assert(vector_write(13,NULL,1)==-1&&errno==EINVAL);
 for(int i=0;i<nx_android_compat_numfunctions;i++){
  DynLibFunction f=nx_android_compat_functions[i];assert(f.func);
  if(!strcmp(f.symbol,"AMEDIAFORMAT_KEY_MIME"))assert(!strcmp(*(const char**)f.func,"mime"));
  puts(f.symbol);
 }
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';p.write_text(pre+code+test)
        subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra',str(p),'-lm','-o',str(exe)],check=True)
        found=set(subprocess.check_output([str(exe)],text=True).splitlines())
    expected=set(json.loads((ROOT/'tools/android_compat_symbols.json').read_text()))
    assert found==expected,(found-expected,expected-found)
    print(f'PASS: {len(found)} missing symbol names, data-pointer ABI, unsupported codec outputs, math/string semantics, atomic vector writes/partial errors')
if __name__=='__main__':main()
