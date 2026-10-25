"""Run real AMD CPU schedulers and the packed-mip layout contract on host."""
from pathlib import Path
import argparse,subprocess,tempfile
from build_fsr31_host import ROOT,HOST
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cxx',required=True);ap.add_argument('--ar',required=True);ap.add_argument('--output',required=True,type=Path);a=ap.parse_args()
 subprocess.run(['python','-X','utf8',str(ROOT/'tools/build_fsr31_host.py'),'--cxx',a.cxx,'--ar',a.ar,'--output',str(a.output)],check=True)
 sources=ROOT/'experimental/framegen/deko';exe=a.output/'sdk_graph_check.exe'
 subprocess.run([a.cxx,'-std=c++17','-O2','-DFFX_GCC','-DNDEBUG','-I'+str(HOST/'include'),str(sources/'sdk_backend.cpp'),str(sources/'sdk_graph_check.cpp'),str(a.output/'libffx_fg_host.a'),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],check=True,capture_output=True,text=True);print(result.stdout,end='');(a.output/'sdk-graph-check.log').write_text(result.stdout)
 code=r'''
#include "pyramid_layout.h"
#include <cassert>
int main(){
 nxfg::PyramidLayout p;
 for(unsigned w: {64u,640u,960u,1280u,4096u})for(unsigned h:{1u,64u,360u,540u,720u}){
  assert(p.Initialize(w,h));unsigned offset=0;
  for(unsigned i=0;i<p.count;i++){assert(p.levels[i].offset==offset);assert(p.levels[i].width&&p.levels[i].height);offset+=p.levels[i].width*p.levels[i].height;}
  assert(p.bytes==offset*8);
  for(unsigned i=p.count;i<13;i++)assert(p.levels[i].width==0&&p.levels[i].height==0);
 }
 assert(!p.Initialize(0,64));assert(!p.Initialize(4097,64));assert(!p.Initialize(64,64,8));assert(!p.Initialize(64,64,14));
 assert(p.Initialize(64,64,1)&&p.count==1&&p.bytes==64*64*8);
}
'''
 # initializer_list is explicit: this tiny check has no SDK dependency.
 src=a.output/'pyramid_layout_check.cpp';src.write_text('#include <initializer_list>\n'+code)
 exe=a.output/'pyramid_layout_check.exe';subprocess.run([a.cxx,'-std=c++17','-O2','-I'+str(sources),str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 print('PASS packed mip layout: 25 sizes, byte extents, absent-mip guards, overflow/range rejection')
if __name__=='__main__':main()
