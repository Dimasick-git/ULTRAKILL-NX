// Run the unchanged SDK CPU schedulers against a resource/dispatch validator.
// No shader execution or generated image is claimed by this host check.
#include "sdk_backend.h"
#include <map>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL: %s:%d %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
#define OK(x) do{FfxErrorCode e=(x);if(e!=FFX_OK){std::fprintf(stderr,"SDK failure 0x%x: %s\n",unsigned(e),#x);std::abort();}}while(0)
struct Resource {FfxResourceDescription desc;unsigned owner;bool alive;};
struct Trace final:nxfg::SdkSink {
 std::vector<Resource> resources{{{},0,false}};
 std::set<std::string> passes;
 unsigned jobs=0,executes=0,pipelines=0,clears=0;
 FfxErrorCode Create(const FfxCreateResourceDescription& d,unsigned owner,FfxResourceInternal& out)override{
  CHECK(d.resourceDescription.width>0);
  out.internalIndex=int(resources.size());resources.push_back({d.resourceDescription,owner,true});
  // SDK's upload helper reserves internalIndex+1 for the initialization copy.
  resources.push_back({{},owner,false});return FFX_OK;
 }
 FfxErrorCode Destroy(FfxResourceInternal r,unsigned)override{
  CHECK(r.internalIndex>=0&&size_t(r.internalIndex)<resources.size());
  resources[r.internalIndex].alive=false;return FFX_OK;
 }
 FfxErrorCode Register(const FfxResource& r,unsigned,FfxResourceInternal& out)override{
  out.internalIndex=int(uintptr_t(r.resource));
  if(out.internalIndex)CHECK(size_t(out.internalIndex)<resources.size()&&resources[out.internalIndex].alive);
  return FFX_OK;
 }
 FfxResource External(FfxResourceInternal r)override{
  FfxResource result{};result.resource=(void*)uintptr_t(r.internalIndex);result.description=Describe(r);return result;
 }
 FfxResourceDescription Describe(FfxResourceInternal r)override{
  CHECK(r.internalIndex>=0&&size_t(r.internalIndex)<resources.size());return resources[r.internalIndex].desc;
 }
 FfxErrorCode Unregister(unsigned)override{return FFX_OK;}
 FfxErrorCode Pipeline(const NxFfxPass& p,const FfxPipelineDescription&,unsigned)override{
  CHECK(p.count>0);pipelines++;return FFX_OK;
 }
 void resource(FfxResourceInternal r){CHECK(r.internalIndex>=0&&size_t(r.internalIndex)<resources.size());if(r.internalIndex)CHECK(resources[r.internalIndex].alive);}
 FfxErrorCode Schedule(const FfxGpuJobDescription& job)override{
  jobs++;
  if(job.jobType==FFX_GPU_JOB_COMPUTE){
   const auto& c=job.computeJobDescriptor;const auto& p=c.pipeline;
   CHECK(c.dimensions[0]&&c.dimensions[1]&&c.dimensions[2]);
   auto meta=(const NxFfxPass*)p.pipeline;CHECK(meta);passes.insert(meta->file);
   for(unsigned i=0;i<p.srvTextureCount;i++)resource(c.srvTextures[i].resource);
   for(unsigned i=0;i<p.uavTextureCount;i++)resource(c.uavTextures[i].resource);
   for(unsigned i=0;i<p.srvBufferCount;i++)resource(c.srvBuffers[i].resource);
   for(unsigned i=0;i<p.uavBufferCount;i++)resource(c.uavBuffers[i].resource);
   for(unsigned i=0;i<p.constCount;i++)CHECK(c.cbs[i].data&&c.cbs[i].num32BitEntries);
  } else if(job.jobType==FFX_GPU_JOB_CLEAR_FLOAT){resource(job.clearJobDescriptor.target);clears++;}
  else if(job.jobType==FFX_GPU_JOB_COPY){resource(job.copyJobDescriptor.src);resource(job.copyJobDescriptor.dst);}
  else if(job.jobType==FFX_GPU_JOB_BARRIER)resource(job.barrierDescriptor.resource);
  else if(job.jobType==FFX_GPU_JOB_DISCARD)resource(job.discardJobDescriptor.target);
  else CHECK(false);
  return FFX_OK;
 }
 FfxErrorCode Execute(FfxCommandList,unsigned)override{executes++;return FFX_OK;}
 FfxErrorCode ReleaseContext(unsigned owner)override{
  for(auto& r:resources)if(r.owner==owner)r.alive=false;return FFX_OK;
 }
 FfxResource shared(const FfxCreateResourceDescription& d){FfxResourceInternal r;OK(Create(d,0,r));return External(r);}
 FfxResource texture(unsigned w,unsigned h,FfxSurfaceFormat format){
  FfxCreateResourceDescription d{};d.resourceDescription={FFX_RESOURCE_TYPE_TEXTURE2D,format,w,h,1,1,FFX_RESOURCE_FLAGS_NONE,FFX_RESOURCE_USAGE_UAV};return shared(d);
 }
};
static void check(unsigned rw,unsigned rh,unsigned dw,unsigned dh){
 Trace trace;nxfg::SdkBackend backend(trace);
 auto optical=std::make_unique<FfxOpticalflowContext>();auto interpolation=std::make_unique<FfxFrameInterpolationContext>();
 FfxOpticalflowContextDescription od{};od.resolution={dw,dh};od.backendInterface=backend.Interface();OK(ffxOpticalflowContextCreate(optical.get(),&od));
 FfxFrameInterpolationContextDescription fd{};fd.maxRenderSize={rw,rh};fd.displaySize={dw,dh};fd.backBufferFormat=fd.previousInterpolationSourceFormat=FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;fd.backendInterface=backend.Interface();
 OK(ffxFrameInterpolationContextCreate(interpolation.get(),&fd));
 CHECK(trace.pipelines==18);
 FfxOpticalflowSharedResourceDescriptions os{};OK(ffxOpticalflowGetSharedResourceDescriptions(optical.get(),&os));
 FfxFrameInterpolationSharedResourceDescriptions fs{};OK(ffxFrameInterpolationGetSharedResourceDescriptions(interpolation.get(),&fs));
 auto color=trace.texture(dw,dh,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM),output=trace.texture(dw,dh,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
 auto depth=trace.texture(rw,rh,FFX_SURFACE_FORMAT_R32_FLOAT),motion=trace.texture(rw,rh,FFX_SURFACE_FORMAT_R16G16_FLOAT);
 auto vectors=trace.shared(os.opticalFlowVector),sceneChange=trace.shared(os.opticalFlowSCD);
 auto dilatedDepth=trace.shared(fs.dilatedDepth),dilatedMotion=trace.shared(fs.dilatedMotionVectors),previousDepth=trace.shared(fs.reconstructedPrevNearestDepth);
 // Includes discontinuous IDs and explicit reset to exercise history reset.
 for(unsigned frame:{0u,1u,2u,10u,11u}){
  FfxOpticalflowDispatchDescription of{};of.commandList=(void*)1;of.color=color;of.opticalFlowVector=vectors;of.opticalFlowSCD=sceneChange;of.reset=frame==0||frame==10;of.minMaxLuminance={0,1};OK(ffxOpticalflowContextDispatch(optical.get(),&of));
  FfxFrameInterpolationPrepareDescription prep{};prep.commandList=(void*)1;prep.renderSize={rw,rh};prep.motionVectorScale={float(rw),float(rh)};prep.frameTimeDelta=33.333f;prep.cameraNear=0.1f;prep.cameraFar=1000;prep.cameraFovAngleVertical=1.2f;prep.viewSpaceToMetersFactor=1;prep.depth=depth;prep.motionVectors=motion;prep.frameID=frame;prep.dilatedDepth=dilatedDepth;prep.dilatedMotionVectors=dilatedMotion;prep.reconstructedPrevDepth=previousDepth;
  OK(ffxFrameInterpolationPrepare(interpolation.get(),&prep));
  FfxFrameInterpolationDispatchDescription fi{};fi.commandList=(void*)1;fi.displaySize={dw,dh};fi.renderSize={rw,rh};fi.currentBackBuffer=color;fi.output=output;fi.interpolationRect={0,0,int32_t(dw),int32_t(dh)};fi.opticalFlowVector=vectors;fi.opticalFlowSceneChangeDetection=sceneChange;fi.opticalFlowBufferSize={os.opticalFlowVector.resourceDescription.width,os.opticalFlowVector.resourceDescription.height};fi.opticalFlowScale={1.0f/dw,1.0f/dh};fi.opticalFlowBlockSize=8;fi.cameraNear=prep.cameraNear;fi.cameraFar=prep.cameraFar;fi.cameraFovAngleVertical=prep.cameraFovAngleVertical;fi.viewSpaceToMetersFactor=1;fi.frameTimeDelta=prep.frameTimeDelta;fi.reset=of.reset;fi.minMaxLuminance[1]=1;fi.frameID=frame;fi.dilatedDepth=dilatedDepth;fi.dilatedMotionVectors=dilatedMotion;fi.reconstructedPrevDepth=previousDepth;
  OK(ffxFrameInterpolationDispatch(interpolation.get(),&fi));
 }
 CHECK(trace.passes.count("ffx_frameinterpolation_pass"));
 CHECK(trace.passes.count("ffx_frameinterpolation_compute_inpainting_pyramid_pass"));
 CHECK(trace.passes.count("ffx_opticalflow_compute_optical_flow_advanced_pass_v5"));
 OK(ffxFrameInterpolationContextDestroy(interpolation.get()));OK(ffxOpticalflowContextDestroy(optical.get()));
 std::printf("PASS SDK graph %ux%u -> %ux%u: %u resources, %u pipelines, %u jobs, %u submissions, %zu passes, resets/ID discontinuity\n",rw,rh,dw,dh,unsigned(trace.resources.size()),trace.pipelines,trace.jobs,trace.executes,trace.passes.size());
}
int main(){check(960,540,1280,720);check(1280,720,1280,720);check(640,360,1280,720);}
