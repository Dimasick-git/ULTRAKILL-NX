"""Exercise production render-thread capture ordering, including nested cameras."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
 code=r'''
#include "nx_fg_capture.h"
#include <cassert>
#include <cstdio>
int main(){
 NxFgCapture state;
 assert(!state.Consume()); // composition before any world draw
 unsigned frames=0;
 for(unsigned i=0;i<1000;i++){
  bool managedWorldPostRender=false;
  state.Mark(); // AfterEverything: actual depth/motion commands completed
  // A nested composition can run before managed world onPostRender listeners.
  assert(!managedWorldPostRender);assert(state.Consume());++frames;
  managedWorldPostRender=true;
  assert(!state.Consume()); // duplicate composition cannot consume stale input
 }
 state.Mark();state.Mark();assert(state.Consume());assert(!state.Consume());
 NxFgCapture newScene;assert(!newScene.Consume());newScene.Mark();assert(newScene.Consume());
 assert(frames==1000);
 puts("PASS: 1000 nested compositions, stale/duplicate rejection, latest capture and scene reset");
}
'''
 with tempfile.TemporaryDirectory() as folder:
  root=Path(folder);src=root/'check.cpp';exe=root/'check.exe';src.write_text(code)
  subprocess.run([a.cxx,'-std=c++17','-O2','-I'+str(ROOT/'loader/source'),str(src),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
