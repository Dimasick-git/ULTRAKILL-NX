"""Run the production Umbra guard against success, rejection and init failure."""
import argparse,subprocess,tempfile
from pathlib import Path
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    src='#include <assert.h>\n'+(root/'loader/source/nx_umbra_guard.h').read_text()+r'''
static int calls,mode,token,input;
static void *init(const void*d,size_t n,void*a){assert(d==&input&&n==123&&!a);++calls;return mode==1?NULL:&token;}
static int status(const void*p){assert(p==&token);++calls;return mode==2?3:0;}
int main(void){
 int why=99;
 assert(!nx_umbra_guard(NULL,123,init,status,&why)&&why==0&&calls==0);
 assert(!nx_umbra_guard(&input,0,init,status,&why)&&calls==0);
 mode=1;assert(!nx_umbra_guard(&input,123,init,status,&why)&&why==-1&&calls==1);
 mode=0;assert(nx_umbra_guard(&input,123,init,status,&why)==&token&&why==0&&calls==3);
 mode=2;assert(!nx_umbra_guard(&input,123,init,status,&why)&&why==3&&calls==5);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(src)
        subprocess.run([args.cc,'-std=c11','-Wall','-Wextra',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: null/empty input, init NULL without status dereference, valid identity, invalid status')
if __name__=='__main__':main()
