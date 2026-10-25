#pragma once
#include "gpu_backend.h"
#include <memory>

static void sdkGpuTest(){
 log("BEGIN: full official AMD optical-flow/frame-interpolation GPU graph\n");
 auto logger=[](const char* message){log("%s\n",message);};
 nxfg::GpuBackend gpu("romfs:/shaders",logger,true);
 CHECK(gpu.Ready());nxfg::SdkBackend backend(gpu);
 auto checked=[&](FfxErrorCode result,const char* stage){
  if(result!=FFX_OK||!gpu.Ready()){log("SDK FAIL: %s code=0x%x backend=%s last_pass=%s\n",stage,unsigned(result),gpu.Error(),gpu.LastPass());fail(stage);}
 };
 auto optical=std::make_unique<FfxOpticalflowContext>();auto interpolation=std::make_unique<FfxFrameInterpolationContext>();
 constexpr unsigned W=192,H=128;
 FfxOpticalflowContextDescription od{};od.resolution={W,H};od.backendInterface=backend.Interface();
 checked(ffxOpticalflowContextCreate(optical.get(),&od),"create optical flow");
 FfxFrameInterpolationContextDescription fd{};fd.maxRenderSize={W,H};fd.displaySize={W,H};fd.backBufferFormat=fd.previousInterpolationSourceFormat=FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;fd.backendInterface=backend.Interface();
 checked(ffxFrameInterpolationContextCreate(interpolation.get(),&fd),"create frame interpolation");
 FfxOpticalflowSharedResourceDescriptions os{};checked(ffxOpticalflowGetSharedResourceDescriptions(optical.get(),&os),"OF shared descriptions");
 FfxFrameInterpolationSharedResourceDescriptions fs{};checked(ffxFrameInterpolationGetSharedResourceDescriptions(interpolation.get(),&fs),"FI shared descriptions");
 auto texture=[&](FfxSurfaceFormat format){FfxCreateResourceDescription d{};d.resourceDescription={FFX_RESOURCE_TYPE_TEXTURE2D,format,W,H,1,1,FFX_RESOURCE_FLAGS_NONE,FFX_RESOURCE_USAGE_UAV};auto r=gpu.Shared(d);CHECK(r.resource);return r;};
 auto color=texture(FFX_SURFACE_FORMAT_R8G8B8A8_UNORM),output=texture(FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
 auto depth=texture(FFX_SURFACE_FORMAT_R32_FLOAT),motion=texture(FFX_SURFACE_FORMAT_R16G16_FLOAT);
 auto vectors=gpu.Shared(os.opticalFlowVector),sceneChange=gpu.Shared(os.opticalFlowSCD);
 auto dilatedDepth=gpu.Shared(fs.dilatedDepth),dilatedMotion=gpu.Shared(fs.dilatedMotionVectors),previousDepth=gpu.Shared(fs.reconstructedPrevNearestDepth);
 CHECK(vectors.resource&&sceneChange.resource&&dilatedDepth.resource&&dilatedMotion.resource&&previousDepth.resource);
 std::vector<DkMemBlock> blocks;
 auto memory=[&](unsigned size){DkMemBlockMaker m;dkMemBlockMakerDefaults(&m,gpu.Device(),(size+4095)&~4095u);m.flags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached|DkMemBlockFlags_ZeroFillInit;auto block=dkMemBlockCreate(&m);CHECK(block);blocks.push_back(block);return block;};
 auto input=memory(W*H*4),z=memory(W*H*4),mv=memory(W*H*4),readback=memory(W*H*4);
 auto pixels=(uint8_t*)dkMemBlockGetCpuAddr(input);for(unsigned i=0;i<W*H;i++){pixels[i*4]=64;pixels[i*4+1]=128;pixels[i*4+2]=192;pixels[i*4+3]=255;((float*)dkMemBlockGetCpuAddr(z))[i]=0.5f;}
 checked(gpu.Upload(color,dkMemBlockGetGpuAddr(input)),"upload color");checked(gpu.Upload(depth,dkMemBlockGetGpuAddr(z)),"upload depth");checked(gpu.Upload(motion,dkMemBlockGetGpuAddr(mv)),"upload motion");
 for(unsigned frame=0;frame<7;frame++){
  bool moving=frame>=3,reset=frame==0||frame==3;
  unsigned boxX=32+8*(moving?frame-3:0);
  if(moving){
   auto motionPixels=(__fp16*)dkMemBlockGetCpuAddr(mv);
   auto depths=(float*)dkMemBlockGetCpuAddr(z);
   for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
    unsigned i=y*W+x;bool box=x>=boxX&&x<boxX+24&&y>=40&&y<72;
    pixels[4*i]=box?255:16;pixels[4*i+1]=box?64:16;pixels[4*i+2]=box?64:16;pixels[4*i+3]=255;
    depths[i]=box?0.4f:0.7f;motionPixels[2*i]=box&&!reset?__fp16(-8.0f/W):__fp16(0);motionPixels[2*i+1]=0;
   }
   checked(gpu.Upload(color,dkMemBlockGetGpuAddr(input)),"moving color upload");
   checked(gpu.Upload(depth,dkMemBlockGetGpuAddr(z)),"moving depth upload");
   checked(gpu.Upload(motion,dkMemBlockGetGpuAddr(mv)),"moving motion upload");
  }
  log("SDK FRAME %u reset=%u moving=%u\n",frame,reset,moving);
  FfxOpticalflowDispatchDescription of{};of.commandList=(void*)1;of.color=color;of.opticalFlowVector=vectors;of.opticalFlowSCD=sceneChange;of.reset=reset;of.minMaxLuminance={0,1};checked(ffxOpticalflowContextDispatch(optical.get(),&of),"dispatch optical flow");
  FfxFrameInterpolationPrepareDescription p{};p.commandList=(void*)1;p.renderSize={W,H};p.motionVectorScale={float(W),float(H)};p.frameTimeDelta=33.333f;p.cameraNear=0.1f;p.cameraFar=1000;p.cameraFovAngleVertical=1.2f;p.viewSpaceToMetersFactor=1;p.depth=depth;p.motionVectors=motion;p.frameID=frame;p.dilatedDepth=dilatedDepth;p.dilatedMotionVectors=dilatedMotion;p.reconstructedPrevDepth=previousDepth;
  checked(ffxFrameInterpolationPrepare(interpolation.get(),&p),"prepare frame interpolation");
  FfxFrameInterpolationDispatchDescription f{};f.commandList=(void*)1;f.displaySize={W,H};f.renderSize={W,H};f.currentBackBuffer=color;f.output=output;f.interpolationRect={0,0,int32_t(W),int32_t(H)};f.opticalFlowVector=vectors;f.opticalFlowSceneChangeDetection=sceneChange;f.opticalFlowBufferSize={os.opticalFlowVector.resourceDescription.width,os.opticalFlowVector.resourceDescription.height};f.opticalFlowScale={1.0f/W,1.0f/H};f.opticalFlowBlockSize=8;f.cameraNear=p.cameraNear;f.cameraFar=p.cameraFar;f.cameraFovAngleVertical=p.cameraFovAngleVertical;f.viewSpaceToMetersFactor=1;f.frameTimeDelta=p.frameTimeDelta;f.reset=of.reset;f.minMaxLuminance[1]=1;f.frameID=frame;f.dilatedDepth=dilatedDepth;f.dilatedMotionVectors=dilatedMotion;f.reconstructedPrevDepth=previousDepth;
  checked(ffxFrameInterpolationDispatch(interpolation.get(),&f),"dispatch frame interpolation");
  checked(gpu.Download(output,dkMemBlockGetGpuAddr(readback)),"download interpolated frame");
  auto actual=(const uint8_t*)dkMemBlockGetCpuAddr(readback);
  if(!moving||reset){
   for(unsigned i=0;i<W*H;i++)for(unsigned channel=0;channel<3;channel++)if(abs(int(actual[i*4+channel])-int(pixels[i*4+channel]))>3){log("STATIC FRAME mismatch frame=%u pixel=%u channel=%u got=%u expected=%u\n",frame,i,channel,actual[i*4+channel],pixels[i*4+channel]);fail("interpolated static frame reference");}
   log("PASS: SDK interpolated reset/static frame %u, all %u RGB pixels vs input\n",frame,W*H);
  }else{
   double weight=0,weightedX=0;
   for(unsigned y=42;y<70;y++)for(unsigned x=0;x<W;x++){
    unsigned i=y*W+x;double red=actual[4*i]>actual[4*i+1]?actual[4*i]-actual[4*i+1]:0;
    weight+=red;weightedX+=red*x;
   }
   CHECK(weight>10000);double center=weightedX/weight,expected=boxX+11.5-4;
   log("MOVING frame=%u centroid=%.3f expected=%.3f current=%.3f\n",frame,center,expected,boxX+11.5);
   CHECK(fabs(center-expected)<2.0);CHECK(fabs(center-(boxX+11.5))>2.0);
   log("PASS: moving frame %u is interpolated halfway, not a repeated input\n",frame);
  }
 }
 checked(ffxFrameInterpolationContextDestroy(interpolation.get()),"destroy FI");checked(ffxOpticalflowContextDestroy(optical.get()),"destroy OF");
 for(auto block:blocks)dkMemBlockDestroy(block);
 log("PASS: full AMD SDK GPU graph, static reference + moving midpoint\n");
}
