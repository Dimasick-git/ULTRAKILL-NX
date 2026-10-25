"""Exercise the production immediate-FPS API without driving another frame."""
import argparse,subprocess,tempfile
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--cc-arg',action='append',default=[]);a=p.parse_args()
 r=Path(__file__).resolve().parents[1];s=(r/'loader/source/main.c').read_text(encoding='utf-8');start=s.index('void nxb_fps_request(int requested)');body=s[start:s.index('\n}',start)+2]
 pre=r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
static NxFpsPolicy g_fps_policy;
static volatile int g_fps_target;
static struct {int fps_limit;} config;
static struct {void* load_virtbase;} unity_mod;
static int overlay,actual,calls;
static int nx_salty_fps_limit(void){return overlay;}
static uint32_t nxo_get(const char *key){assert(!strcmp(key,"unity.app_target_fps"));return 4;}
static void fake_set(int value){actual=value;++calls;}
#define debugPrintf(...) ((void)0)
"""
 test=r"""
int main(void){
 unity_mod.load_virtbase=(void*)((uintptr_t)&fake_set-4);
 nxb_fps_request(144);assert(calls==1 && actual==144 && g_fps_target==144);
 nxb_fps_request(30);assert(calls==2 && actual==30);
 nxb_fps_request(-1);assert(calls==3 && actual==1000);
 overlay=30;nxb_fps_request(144);assert(calls==4 && actual==30 && g_fps_policy.requested==144);
 nxb_fps_request(30);assert(calls==5 && actual==30 && g_fps_policy.requested==30);
 overlay=0;assert(nx_fps_observe(&g_fps_policy,actual,0,overlay)==30);
 nxb_fps_request(120);assert(calls==6 && actual==120);
 return 0;
}
"""
 with tempfile.TemporaryDirectory() as tmp:
  c=Path(tmp)/'request.c';exe=Path(tmp)/'request.exe';c.write_text((r/'loader/source/nx_fps_policy.h').read_text()+pre+body+test)
  subprocess.run([a.cc,*a.cc_arg,'-std=c11','-O2','-UNDEBUG',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS: native setter called immediately for 30/120/144/None, without another frame; explicit overlay-equal selection retained')
if __name__=='__main__':main()
