"""Inject OOM into real backing allocator; test real FG cost guard."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
 code=r'''
#include "owned_memory.h"
#include "nx_fg_budget.h"
#include <cassert>
#include <cstdio>
static bool failNode,failData;static int live,alignedCalls;
void* plain(size_t size){if(failNode)return nullptr;void* p=std::malloc(size);if(p)++live;return p;}
void* aligned(size_t alignment,size_t size){++alignedCalls;assert(size%alignment==0);if(failData)return nullptr;void* p=nxfg::OwnedAligned(alignment,size);if(p)++live;return p;}
void release(void* p){if(p){--live;std::free(p);}}
void releaseData(void* p){if(p){--live;nxfg::OwnedAlignedFree(p);}}
int main(){
 nxfg::OwnedMemory memory(aligned,plain,release,releaseData);
 failNode=true;assert(!memory.Allocate(100,4096));assert(!alignedCalls&&!live);failNode=false;
 failData=true;assert(!memory.Allocate(100,4096));assert(!live);failData=false;
 assert(!memory.Allocate(SIZE_MAX,4096));assert(!memory.Allocate(8,3));assert(!memory.Allocate(0,4096));
 void* a=memory.Allocate(100,4096);void* b=memory.Allocate(8193,4096);
 assert(a&&b&&!(uintptr_t(a)%4096)&&live==4);
 // Storage remains alive while GPU handles exist; explicit release is after
 // device teardown, and can be omitted for quarantine without freeing it.
 assert(live==4);memory.Release();assert(!live);memory.Release();assert(!live);
 NxFgBudget budget;assert(!budget.Observe(66,true));assert(!budget.Observe(123,false));assert(budget.Observe(121,false));
 NxFgBudget spikes;assert(!spikes.Observe(99,true));assert(!spikes.Observe(30,false));assert(!spikes.Observe(8,false));assert(!spikes.Observe(30,false));assert(!spikes.Observe(8,false));
 NxFgBudget cuts;assert(!cuts.Observe(66,true));assert(!cuts.Observe(66,true));assert(cuts.Observe(66,true));
 puts("PASS: node/backing OOM without entering deko, alignment/overflow, explicit GPU-safe release, sustained-cost fallback and camera resets");
}
'''
 with tempfile.TemporaryDirectory() as folder:
  root=Path(folder);src=root/'check.cpp';exe=root/'check.exe';src.write_text(code)
  subprocess.run([a.cxx,'-std=c++17','-O2','-I'+str(ROOT/'experimental/framegen/deko'),'-I'+str(ROOT/'loader/source'),str(src),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 source=(ROOT/'experimental/framegen/deko/gpu_backend.cpp').read_text()
 body=source[source.index('DkMemBlock GpuBackend::allocate'):source.index('GpuBackend::GpuBackend')]
 assert body.index('ownedMemory_.Allocate')<body.index('dkMemBlockCreate')
 assert 'if(!maker.storage)' in body
if __name__=='__main__':main()
