#include <switch.h>
#include <EGL/egl.h>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include "nx_fg_runtime.h"
#include "nx_fg_interop.h"
extern "C" {
#include "nx_gameroot.h"
int debugPrintf(char*,...);
}

namespace {
// Stop retrying after unknown GPU ownership. A scene transition must not free
// or reuse buffers still referenced by a failed GPU queue.
std::atomic<bool> gpuFault{false};
struct Session {
 const uint32_t color,width,height;
 std::atomic<int> status{0};
 DkDevice device=nullptr;DkQueue queue=nullptr;DkCmdBuf commands=nullptr;
 DkMemBlock blocks[8]{};unsigned count=0;
 DkShader shader{};DkImage inputImage{},lumaImage{};
 nxfg::Transfer transfer;
 bool quarantined=false;
 Session(uint32_t c,uint32_t w,uint32_t h):color(c),width(w),height(h){}
 bool fail(const char* reason){
  status.store(-1,std::memory_order_release);
  debugPrintf((char*)"[fg-bringup] disabled: %s (no interpolated frame presented)\n",reason);return false;
 }
 DkMemBlock alloc(uint32_t size,uint32_t extra=0){
  if(count>=8||size>0xffff0000u)return nullptr;
  DkMemBlockMaker maker;dkMemBlockMakerDefaults(&maker,device,(size+4095u)&~4095u);
  maker.flags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached|extra;
  DkMemBlock result=dkMemBlockCreate(&maker);if(result)blocks[count++]=result;return result;
 }
 bool image(DkImage& result,DkImageFormat format,uint32_t flags){
  DkImageLayoutMaker maker;dkImageLayoutMakerDefaults(&maker,device);
  maker.format=format;maker.flags=flags;maker.dimensions[0]=width;maker.dimensions[1]=height;maker.dimensions[2]=1;
  DkImageLayout layout;dkImageLayoutInitialize(&layout,&maker);
  uint64_t size=dkImageLayoutGetSize(&layout);if(size>64*1024*1024)return false;
  DkMemBlock memory=alloc(uint32_t(size),DkMemBlockFlags_Image);if(!memory)return false;
  dkImageInitialize(&result,&layout,memory,0);return true;
 }
 bool initialize(){
  if(eglGetCurrentContext()==EGL_NO_CONTEXT)return fail("callback has no GLES context");
  // Match the SDK's ceil-divided dispatch, including normal 540p/720p heights.
  // Image descriptors retain actual dimensions; extra edge lanes cannot store
  // beyond them. The official host uses the same partially covered workgroups.
  char path[768];game_path(path,sizeof path,"assets/NxPort/framegen/ffx_opticalflow_prepare_luma_pass.dksh");
  FILE* file=fopen(path,"rb");if(!file)return fail("prepare_luma DKSH missing");
  uint32_t header[6];bool valid=fread(header,sizeof header,1,file)==1&&header[0]==0x48534b44&&header[1]==24&&header[5]==1&&header[2]>=24&&header[2]<65536&&header[3]>0&&header[3]<4*1024*1024;
  if(!valid){fclose(file);return fail("invalid DKSH header");}
  void* control=malloc(header[2]);if(!control){fclose(file);return fail("DKSH control allocation failed");}
  rewind(file);valid=fread(control,header[2],1,file)==1;
  DkDeviceMaker dm;dkDeviceMakerDefaults(&dm);device=dkDeviceCreate(&dm);
  if(!device){free(control);fclose(file);return fail("deko device creation failed");}
  DkQueueMaker qm;dkQueueMakerDefaults(&qm,device);qm.perWarpScratchMemorySize=8192;
  queue=dkQueueCreate(&qm);if(!queue){free(control);fclose(file);return fail("deko queue creation failed");}
  auto code=alloc(header[3]+DK_SHADER_CODE_UNUSABLE_SIZE,DkMemBlockFlags_Code);
  valid=valid&&code&&fread(dkMemBlockGetCpuAddr(code),header[3],1,file)==1&&fgetc(file)==EOF;fclose(file);
  if(!valid){free(control);return fail("truncated DKSH/code allocation failed");}
  DkShaderMaker sm;dkShaderMakerDefaults(&sm,code,0);sm.control=control;dkShaderInitialize(&shader,&sm);free(control);
  if(!dkShaderIsValid(&shader)||dkShaderGetStage(&shader)!=DkStage_Compute)return fail("invalid compute shader");
  if(!transfer.Initialize(device,width,height,GL_RGBA,GL_UNSIGNED_BYTE,4))return fail(transfer.Error());
  if(!image(inputImage,DkImageFormat_RGBA8_Unorm,0)||!image(lumaImage,DkImageFormat_R8_Uint,DkImageFlags_UsageLoadStore))return fail("FG image allocation failed");
  auto commandMemory=alloc(65536),constants=alloc(256),descriptors=alloc(256),samplers=alloc(256);
  if(!commandMemory||!constants||!descriptors||!samplers)return fail("FG command/binding allocation failed");
  uint32_t params[8]={width,height,0,7,0,0,0,0};memcpy(dkMemBlockGetCpuAddr(constants),params,sizeof params);
  DkImageView inputView,outputView;dkImageViewDefaults(&inputView,&inputImage);dkImageViewDefaults(&outputView,&lumaImage);
  auto descriptor=(DkImageDescriptor*)dkMemBlockGetCpuAddr(descriptors);
  dkImageDescriptorInitialize(descriptor,&inputView,false,false);dkImageDescriptorInitialize(descriptor+1,&outputView,true,false);
  DkSampler sampler;dkSamplerDefaults(&sampler);dkSamplerDescriptorInitialize((DkSamplerDescriptor*)dkMemBlockGetCpuAddr(samplers),&sampler);
  DkCmdBufMaker cm;dkCmdBufMakerDefaults(&cm,device);commands=dkCmdBufCreate(&cm);
  if(!commands)return fail("FG command buffer allocation failed");
  dkCmdBufAddMemory(commands,commandMemory,0,65536);
  DkCopyBuf copy{transfer.Address(),0,0};DkImageRect rect{0,0,0,width,height,1};
  dkCmdBufCopyBufferToImage(commands,&copy,&inputView,&rect,0);
  dkCmdBufBarrier(commands,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
  const DkShader* sh=&shader;dkCmdBufBindShaders(commands,DkStageFlag_Compute,&sh,1);
  dkCmdBufBindImageDescriptorSet(commands,dkMemBlockGetGpuAddr(descriptors),2);dkCmdBufBindSamplerDescriptorSet(commands,dkMemBlockGetGpuAddr(samplers),1);
  // Pinned FP32/SDR prepare_luma profile: cbOF=0, input texture=0, output image=0.
  DkBufExtents uniform{dkMemBlockGetGpuAddr(constants),32};dkCmdBufBindUniformBuffers(commands,DkStage_Compute,0,&uniform,1);
  DkResHandle texture=dkMakeTextureHandle(0,0),output=dkMakeImageHandle(1);
  dkCmdBufBindTextures(commands,DkStage_Compute,0,&texture,1);dkCmdBufBindImages(commands,DkStage_Compute,0,&output,1);
  dkCmdBufDispatchCompute(commands,(width+31)/32,(height+31)/32,1);
  return true;
 }
 void render(){
  if(status.load(std::memory_order_acquire)<0||gpuFault.load(std::memory_order_acquire))return;
  if(!device&&!initialize())return;
  u64 start=armGetSystemTick();
  if(!transfer.Capture(color)){quarantined=true;gpuFault.store(true);transfer.Quarantine();fail(transfer.Error());return;}
  // This bring-up session dispatches once. A future streaming backend must use
  // persistent per-slot fences rather than reusing this invocation's stack.
  DkFence fence;
  dkCmdBufBarrier(commands,DkBarrier_Full,DkInvalidateFlags_L2Cache);
  dkCmdBufSignalFence(commands,&fence,true);
  dkQueueSubmitCommands(queue,dkCmdBufFinishList(commands));dkQueueFlush(queue);
  auto result=dkFenceWait(&fence,2000000000LL);
  if(result!=DkResult_Success||dkQueueIsInErrorState(queue)){
   quarantined=true;transfer.Quarantine();gpuFault.store(true);fail("deko producer fence failed");return;
  }
  // finishList consumes the recorded list. Sampling once is sufficient for
  // this handshake; no later frame can refer to the retired stack fence.
  status.store(1,std::memory_order_release);
  debugPrintf((char*)"[fg-bringup] GLES->deko AMD luma %ux%u completed in %.2f ms; FG=not-integrated\n",width,height,
    double(armGetSystemTick()-start)*1000.0/19200000.0);
 }
};
void renderEvent(int event,void* data){
 auto session=(Session*)data;if(!session)return;
 if(event==1){if(session->status.load()==0)session->render();}
 else if(event==2){
  if(session->quarantined)return;
  if(session->queue)dkQueueWaitIdle(session->queue);
  // Preserve deko handles while the Transfer member unregisters shared pages.
  auto device=session->device;auto queue=session->queue;auto commands=session->commands;
  DkMemBlock blocks[8];unsigned count=session->count;memcpy(blocks,session->blocks,sizeof blocks);
  delete session;
  if(commands)dkCmdBufDestroy(commands);
  if(queue)dkQueueDestroy(queue);
  for(unsigned i=0;i<count;i++)dkMemBlockDestroy(blocks[i]);
  if(device)dkDeviceDestroy(device);
 }
}
}
extern "C" void* nxb_fg_probe_create(uint32_t color,uint32_t width,uint32_t height){
 if(!color||!width||!height||width>4096||height>4096||gpuFault.load())return nullptr;
 return new(std::nothrow)Session(color,width,height);
}
extern "C" void* nxb_fg_probe_callback(){return (void*)&renderEvent;}
extern "C" int nxb_fg_probe_status(void* data){return data?((Session*)data)->status.load(std::memory_order_acquire):-1;}
