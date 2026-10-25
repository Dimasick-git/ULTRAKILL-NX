#include "sdk_backend.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace nxfg {
namespace {
void ascii(wchar_t* dst,size_t cap,const char* src){
 size_t i=0;for(;i+1<cap&&src[i];i++)dst[i]=(unsigned char)src[i];dst[i]=0;
}
void bind(FfxResourceBinding& result,const char* name,unsigned slot){
 result={};result.slotIndex=slot;ascii(result.name,FFX_RESOURCE_NAME_SIZE,name);
}
}
SdkBackend::SdkBackend(SdkSink& sink):sink_(sink){
 auto& b=interface_;b.scratchBuffer=this;b.scratchBufferSize=sizeof(*this);b.device=this;
 b.fpGetSDKVersion=[](FfxInterface*)->FfxVersionNumber{return FFX_SDK_MAKE_VERSION(FFX_SDK_VERSION_MAJOR,FFX_SDK_VERSION_MINOR,FFX_SDK_VERSION_PATCH);};
 b.fpCreateBackendContext=[](FfxInterface* b,FfxEffect,FfxEffectBindlessConfig* bindless,FfxUInt32* id)->FfxErrorCode{
  if(!id||bindless)return FFX_ERROR_INVALID_ARGUMENT;*id=get(b).nextContext_++;return FFX_OK;
 };
 b.fpDestroyBackendContext=[](FfxInterface* b,FfxUInt32 id){return get(b).sink_.ReleaseContext(id);};
 b.fpGetDeviceCapabilities=[](FfxInterface*,FfxDeviceCapabilities* cap)->FfxErrorCode{
  if(!cap)return FFX_ERROR_INVALID_POINTER;
  *cap={};cap->maximumSupportedShaderModel=FFX_SHADER_MODEL_6_0;cap->waveLaneCountMin=cap->waveLaneCountMax=32;
  // Compiled profile is FP32. Do not let SDK choose wave64/half variants.
  cap->fp16Supported=false;return FFX_OK;
 };
 b.fpCreateResource=[](FfxInterface* b,const FfxCreateResourceDescription* desc,FfxUInt32 id,FfxResourceInternal* out)->FfxErrorCode{
  return desc&&out?get(b).sink_.Create(*desc,id,*out):FFX_ERROR_INVALID_POINTER;
 };
 b.fpDestroyResource=[](FfxInterface* b,FfxResourceInternal r,FfxUInt32 id){return get(b).sink_.Destroy(r,id);};
 b.fpRegisterResource=[](FfxInterface* b,const FfxResource* r,FfxUInt32 id,FfxResourceInternal* out)->FfxErrorCode{
  return r&&out?get(b).sink_.Register(*r,id,*out):FFX_ERROR_INVALID_POINTER;
 };
 b.fpUnregisterResources=[](FfxInterface* b,FfxCommandList,FfxUInt32 id){return get(b).sink_.Unregister(id);};
 b.fpGetResource=[](FfxInterface* b,FfxResourceInternal r){return get(b).sink_.External(r);};
 b.fpGetResourceDescription=[](FfxInterface* b,FfxResourceInternal r){return get(b).sink_.Describe(r);};
 b.fpStageConstantBufferDataFunc=[](FfxInterface* b,void* data,FfxUInt32 size,FfxConstantBuffer* out)->FfxErrorCode{
  if(!data||!out||!size||size%4)return FFX_ERROR_INVALID_ARGUMENT;
  auto& v=get(b).constants_.emplace_back(size/4);std::memcpy(v.data(),data,size);
  out->num32BitEntries=size/4;out->data=v.data();return FFX_OK;
 };
 b.fpCreatePipeline=pipeline;
 b.fpDestroyPipeline=[](FfxInterface*,FfxPipelineState* p,FfxUInt32)->FfxErrorCode{if(p)*p={};return FFX_OK;};
 b.fpScheduleGpuJob=[](FfxInterface* b,const FfxGpuJobDescription* job)->FfxErrorCode{return job?get(b).sink_.Schedule(*job):FFX_ERROR_INVALID_POINTER;};
 b.fpExecuteGpuJobs=[](FfxInterface* b,FfxCommandList list,FfxUInt32 id){
  auto& self=get(b);FfxErrorCode result=self.sink_.Execute(list,id);self.constants_.clear();return result;
 };
 b.fpGetEffectGpuMemoryUsage=[](FfxInterface*,FfxUInt32,FfxEffectMemoryUsage* out)->FfxErrorCode{if(!out)return FFX_ERROR_INVALID_POINTER;*out={};return FFX_OK;};
}
FfxErrorCode SdkBackend::pipeline(FfxInterface* b,FfxEffect effect,FfxPass pass,uint32_t options,
 const FfxPipelineDescription* desc,FfxUInt32 context,FfxPipelineState* out){
 if(!desc||!out)return FFX_ERROR_INVALID_POINTER;
 const unsigned id=effect==FFX_EFFECT_OPTICALFLOW?1:effect==FFX_EFFECT_FRAMEINTERPOLATION?2:0;
 // This metadata is specifically low-res motion, standard depth, no jitter.
 // Reject a mismatched profile instead of dispatching the wrong DKSH silently.
 if(!id||(id==1&&options!=0)||(id==2&&options!=1))return FFX_ERROR_INVALID_ARGUMENT;
 const NxFfxPass* meta=nullptr;
 for(const auto& item:nx_ffx_passes)if(item.effect==id&&item.pass==pass){meta=&item;break;}
 if(!meta)return FFX_ERROR_INVALID_ARGUMENT;
 *out={};out->passId=pass;out->pipeline=(void*)meta;wcscpy_s(out->name,desc->name);
 for(unsigned i=0;i<meta->count;i++){
  auto& resource=meta->bindings[i];FfxResourceBinding* dst=nullptr;
  if(!strcmp(resource.name,"nxPyramid")||!strcmp(resource.name,"nxPyramidLayout"))continue;
  if(!strcmp(resource.kind,"texture"))dst=&out->srvTextureBindings[out->srvTextureCount++];
  else if(!strcmp(resource.kind,"image"))dst=&out->uavTextureBindings[out->uavTextureCount++];
  else if(!strcmp(resource.kind,"uniform")){
   if(out->constCount>=FFX_MAX_NUM_CONST_BUFFERS)return FFX_ERROR_OUT_OF_RANGE;
   dst=&out->constantBufferBindings[out->constCount++];
  }
  else if(!strcmp(resource.kind,"storage")){
   if(!strncmp(resource.name,"rw_",3))dst=&out->uavBufferBindings[out->uavBufferCount++];
   else dst=&out->srvBufferBindings[out->srvBufferCount++];
  }
  if(!dst)return FFX_ERROR_INVALID_ARGUMENT;
  bind(*dst,resource.name,resource.slot);
 }
 // Keep virtual SDK mip UAVs in the job, even though deko writes a packed SSBO.
 // The executor uses their resource/mip references to fill layout and copy the
 // thirteen linear ranges into the sampled mipmapped image after dispatch.
 if(meta->packedPyramid)for(unsigned mip=0;mip<13;mip++){
  char name[64];std::snprintf(name,sizeof name,"rw_inpainting_pyramid%u",mip);
  bind(out->uavTextureBindings[out->uavTextureCount++],name,mip);
 }
 return get(b).sink_.Pipeline(*meta,*desc,context);
}
}
