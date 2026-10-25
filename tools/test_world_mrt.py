"""Compile the emitted MRT repair: restore lost binding each frame, no allocations."""
from pathlib import Path
import argparse,subprocess,tempfile
from patch_world_mrt import patch
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cxx',default='g++');a=ap.parse_args()
 code=r'''
#include <cassert>
#include <cstddef>
struct Texture {int id;};struct Buffers {size_t max_length=3;int data[3]={};void SetAt(int i,int v){assert(i>=0&&i<3);data[i]=v;}};
struct Camera {int color[3]={},depth=0;};
struct Post {Camera *___mainCam;Texture *___mainTex,*___reusableBufferA,*___viewNormal,*___depthBuffer;Buffers *___buffers;};
static int calls,allocations;
int RenderTexture_get_colorBuffer_mAA(Texture *t,void*){return t->id;}
int RenderTexture_get_depthBuffer_mBB(Texture *t,void*){return t->id;}
void Camera_SetTargetBuffers_mCC(Camera *c,Buffers *b,int depth,void*){for(int i=0;i<3;i++)c->color[i]=b->data[i];c->depth=depth;calls++;}
void reference(Camera *L_97,Buffers *L_98,int L_100){Camera_SetTargetBuffers_mCC(L_97, L_98, L_100, NULL);}
void PostProcessV2_Handler_SetupRTs_mDD(Post*,void*){}
void NestedRender(Post* p){for(int i=0;i<3;i++)p->___mainCam->color[i]=0;p->___mainCam->depth=0;}
#define IL2CPP_EXTERN_C
#define IL2CPP_METHOD_ATTR
IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void PostProcessV2_Handler_OnPreRenderCallback_mEE (Post *__this, Camera *___0_cam, void*)
{
		PostProcessV2_Handler_SetupRTs_mDD(__this, NULL);
        if(___0_cam==__this->___mainCam)NestedRender(__this);
        goto IL_048e;
IL_048e:
	{
		return;
	}
}
// Method Definition Index: 9999
int main(){
 Camera world;Texture color{1},outline{2},normal{3},depth{4};Buffers ___buffers;Post post{&world,&color,&outline,&normal,&depth,&___buffers};
 for(int frame=0;frame<100;frame++){
  world.color[0]=world.color[1]=world.color[2]=world.depth=0; // emulate native target loss with live RT objects
  color.id=10+frame;
  PostProcessV2_Handler_OnPreRenderCallback_mEE(&post,&world,nullptr);
  assert(world.color[0]==10+frame&&world.color[1]==2&&world.color[2]==3&&world.depth==4);
 }
 assert(calls==100&&allocations==0);
 Camera hud;PostProcessV2_Handler_OnPreRenderCallback_mEE(&post,&hud,nullptr);assert(calls==100);
 ___buffers.max_length=2;PostProcessV2_Handler_OnPreRenderCallback_mEE(&post,&world,nullptr);assert(calls==100);
 ___buffers.max_length=3;post.___depthBuffer=nullptr;PostProcessV2_Handler_OnPreRenderCallback_mEE(&post,&world,nullptr);assert(calls==100);
 return 0;
}
'''
 fixed=patch(code);assert patch(fixed)==fixed
 for bad in (code.replace('PostProcessV2_Handler_SetupRTs_mDD(__this, NULL);',''),code.replace('Camera_SetTargetBuffers_mCC(L_97, L_98, L_100, NULL);',''),code.replace('IL_048e:', 'IL_CHANGED:')):
  try:patch(bad)
  except ValueError:pass
  else:raise AssertionError('Changed generated ABI must fail closed')
 with tempfile.TemporaryDirectory() as t:
  p=Path(t)/'test.cpp';exe=Path(t)/'test.exe';p.write_text(fixed)
  subprocess.run([a.cxx,'-std=c++17','-O2','-Wall','-Wextra',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 print('PASS: MRT restored AFTER 100 nested renders, HUD untouched, no allocations, invalid targets guarded, ABI drift rejected')
if __name__=='__main__':main()
