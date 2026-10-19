"""Exercise the real overlay request parser and render acknowledgment."""
from pathlib import Path
import argparse,re,subprocess,tempfile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    header=(root/'loader/source/nx_render_control.h').read_text().replace('#pragma once','')
    code=(root/'loader/source/nx_render_control.c').read_text()
    code=re.sub(r'^#include "[^"\n]+"\n','',code,flags=re.M)
    pre=r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
static struct {int render_height,fsr;float fsr_sharpness;} config={540,1,.5f};
static FILE* no_config(const char* a,const char* b){(void)a;(void)b;return NULL;}
#define fopen no_config
#define debugPrintf(...) ((void)0)
'''
    test=r'''
int main(void){
 uint32_t region[(128+24)/4]={0};nx_render_shared_init(region);
 assert(offsetof(NxFsrShared,sharpness)==8 && offsetof(NxFsrShared,scale_percent)==124);
 assert(shared->magic==0x31525346 && live->magic==0x43525346 && live->active==5);
 assert(nx_render_height()==540 && nx_render_filter()==1 && live->scale==75);
 live->seq=0x80000001;live->scale=50;shared->sharpness=.7f;
 assert(nx_render_height()==540); // half-published request must not apply
 live->seq=1;live->upscaler=0x81;live->depth=1;
 assert(nx_render_height()==360 && nx_render_sharpness()==.7f && !live->applied);
 assert(live->upscaler==0x80 && live->depth==0); // unsupported options not falsely applied
 nxb_fsr_applied(960,540,1280,720,1);assert(!live->applied);
 nxb_fsr_applied(640,360,1280,720,1);assert(live->applied==1 && shared->scale_percent==50);
 live->scale=15;live->fsr=0;live->seq=2;
 assert(nx_render_height()==180 && !nx_render_filter() && live->scale==25);
 nxb_fsr_applied(320,180,1280,720,0);assert(live->applied==2 && !shared->enabled);
 live->scale=200;live->seq=3;assert(nx_render_height()==720 && live->scale==100);
 live->scale=0;live->seq=4;assert(nx_render_height()==720 && revision==3);
 live->scale=50;shared->sharpness=NAN;live->seq=5;assert(nx_render_height()==720 && revision==3);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'control.c';exe=Path(tmp)/'control.exe';src.write_text(header+pre+code+test)
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: exact FSR overlay ABI, coherent publication, bounded scale, filter/sharpness, real-size acknowledgment, invalid requests')

if __name__=='__main__':main()
