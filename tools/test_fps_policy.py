"""Compile and exercise the production native limiter policy on the host."""
import argparse,subprocess,tempfile
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--cc-arg',action='append',default=[]);a=p.parse_args()
 header=Path(__file__).resolve().parents[1]/'loader/source/nx_fps_policy.h'
 source='#include <assert.h>\n'+header.read_text()+r"""
int main(void) {
 NxFpsPolicy state={0};
 assert(nx_fps_observe(&state,-1,0,0)==1000); /* None */
 assert(nx_fps_observe(&state,1000,0,0)==1000);
 assert(nx_fps_observe(&state,120,0,0)==120); /* old saved PC cap */
 assert(nx_fps_observe(&state,120,0,40)==40);
 assert(nx_fps_observe(&state,40,0,0)==120); /* overlay released */
 assert(nx_fps_observe(&state,30,0,0)==30); /* game changed */
 assert(nx_fps_observe(&state,30,60,0)==60); /* config override */
 assert(nx_fps_observe(&state,60,0,0)==30); /* override released */
 assert(nx_fps_observe(&state,-1,0,120)==120);
 assert(nx_fps_observe(&state,120,0,0)==1000);
 // Selecting the same number as the current overlay must still change the
 // stored game request, so releasing the overlay cannot undo that selection.
 assert(nx_fps_select(&state,144,0,30)==30);
 assert(nx_fps_select(&state,30,0,30)==30);
 assert(nx_fps_observe(&state,30,0,0)==30);
 assert(nx_fps_select(&state,144,0,0)==144);
 int rates[]={30,60,120,144,240,288,-1};
 for(unsigned i=0;i<sizeof rates/sizeof rates[0];++i) {
  int expected=rates[i]<0?1000:rates[i];
  assert(nx_fps_observe(&state,rates[i],0,0)==expected);
 }
 for(int requested=-2;requested<=1000;++requested) {
  int result=nx_fps_resolve(requested,0,0);
  assert(result>0 && result<=1000);
 }
 assert(nx_fps_resolve(60,0,20)==20);
 assert(nx_fps_resolve(60,0,1)==60); /* invalid overlay sentinel */
 return 0;
}
"""
 with tempfile.TemporaryDirectory() as tmp:
  c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(source)
  subprocess.run([a.cc,*a.cc_arg,'-std=c11','-UNDEBUG','-Wall','-Wextra',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS: None=1000 and explicit PC rates preserved; lower caps; overlay/override release restores game request')
if __name__=='__main__':main()
