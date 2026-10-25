#include "gpu_backend.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <switch.h>

namespace nxfg {
namespace {
constexpr unsigned ScratchBytes=4*1024*1024,CommandBytes=1024*1024;
DkImageFormat format(FfxSurfaceFormat f){
 switch(f){
 #define F(A,B) case FFX_SURFACE_FORMAT_##A:return DkImageFormat_##B
 F(R8_UINT,R8_Uint);F(R8_UNORM,R8_Unorm);F(R8G8_UNORM,RG8_Unorm);F(R8G8_UINT,RG8_Uint);
 F(R16_FLOAT,R16_Float);F(R16_UINT,R16_Uint);F(R16_SNORM,R16_Snorm);F(R16_UNORM,R16_Unorm);
 F(R16G16_FLOAT,RG16_Float);F(R16G16_SINT,RG16_Sint);F(R16G16_UINT,RG16_Uint);
 F(R32_FLOAT,R32_Float);F(R32_UINT,R32_Uint);F(R32G32_FLOAT,RG32_Float);
 F(R8G8B8A8_UNORM,RGBA8_Unorm);F(R8G8B8A8_TYPELESS,RGBA8_Unorm);
 F(R16G16B16A16_FLOAT,RGBA16_Float);F(R16G16B16A16_TYPELESS,RGBA16_Float);
 F(R32G32B32A32_FLOAT,RGBA32_Float);F(R32G32B32A32_UINT,RGBA32_Uint);
 #undef F
 default:return DkImageFormat_None;
 }
}
DkImageView view(DkImage& image,unsigned mip=0){
 DkImageView result;dkImageViewDefaults(&result,&image);result.mipLevelOffset=mip;return result;
}
int binding(const FfxResourceBinding* b,unsigned count,const char* name){
 for(unsigned i=0;i<count;i++){
  unsigned n=0;for(;name[n]&&b[i].name[n]==(unsigned char)name[n];n++){}
  if(!name[n]&&!b[i].name[n])return int(i);
 }
 return -1;
}
}
FfxErrorCode GpuBackend::fail(const char* text){failed_=true;error_=text;return FFX_ERROR_BACKEND_API_ERROR;}
DkMemBlock GpuBackend::allocate(uint32_t size,uint32_t extra){
 if(!size||size>256*1024*1024){fail("invalid GPU allocation size");return nullptr;}
 DkMemBlockMaker maker;dkMemBlockMakerDefaults(&maker,device_,(size+4095u)&~4095u);
 maker.storage=ownedMemory_.Allocate(size,4096);
 if(!maker.storage){fail("GPU backing allocation failed before deko");return nullptr;}
 maker.flags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached|DkMemBlockFlags_ZeroFillInit|extra;
 auto memory=dkMemBlockCreate(&maker);if(!memory)fail("GPU allocation failed");return memory;
}
GpuBackend::GpuBackend(const char* root,void (*logger)(const char*),bool traceJobs):shaderRoot_(root?root:""),logger_(logger),traceJobs_(traceJobs){
 resources_.emplace_back();DkDeviceMaker dm;dkDeviceMakerDefaults(&dm);dm.userData=this;
 dm.cbDebug=[](void* userdata,const char* context,DkResult result,const char* message){
  auto self=(GpuBackend*)userdata;
  if(self->logger_){char text[512];std::snprintf(text,sizeof text,"deko SDK: %s result=%d %s",context,int(result),message);self->logger_(text);}
  if(result!=DkResult_Success)self->fail("deko SDK validation error");
 };
 device_=dkDeviceCreate(&dm);
 if(!device_){fail("deko device creation failed");return;}
 DkQueueMaker qm;dkQueueMakerDefaults(&qm,device_);qm.perWarpScratchMemorySize=8192;
 queue_=dkQueueCreate(&qm);if(!queue_){fail("deko queue creation failed");return;}
 commandMemory_=allocate(CommandBytes);scratch_=allocate(ScratchBytes);samplers_=allocate(256);
 if(failed_)return;
 DkSampler sampler;dkSamplerDefaults(&sampler);for(int i=0;i<3;i++)sampler.wrapMode[i]=DkWrapMode_ClampToEdge;
 auto descriptors=(DkSamplerDescriptor*)dkMemBlockGetCpuAddr(samplers_);
 dkSamplerDescriptorInitialize(descriptors,&sampler);sampler.minFilter=sampler.magFilter=DkFilter_Linear;
 dkSamplerDescriptorInitialize(descriptors+1,&sampler);
 DkCmdBufMaker cm;dkCmdBufMakerDefaults(&cm,device_);commands_=dkCmdBufCreate(&cm);
 if(!commands_){fail("deko command buffer creation failed");return;}
 dkCmdBufAddMemory(commands_,commandMemory_,0,CommandBytes);error_="ready";
}
GpuBackend::~GpuBackend(){
 if(quarantined_)return;
 if(queue_)dkQueueWaitIdle(queue_);
 if(commands_)dkCmdBufDestroy(commands_);
 if(queue_)dkQueueDestroy(queue_);
 for(auto& r:resources_){if(r.packed)dkMemBlockDestroy(r.packed);if(r.memory)dkMemBlockDestroy(r.memory);}
 for(auto& p:programs_)if(p.second.code)dkMemBlockDestroy(p.second.code);
 if(samplers_)dkMemBlockDestroy(samplers_);if(scratch_)dkMemBlockDestroy(scratch_);if(commandMemory_)dkMemBlockDestroy(commandMemory_);
 if(device_)dkDeviceDestroy(device_);
 ownedMemory_.Release();
}
GpuBackend::Resource* GpuBackend::resource(FfxResourceInternal r){
 if(r.internalIndex<=0||unsigned(r.internalIndex)>=resources_.size()||!resources_[r.internalIndex].alive){fail("invalid SDK resource reference");return nullptr;}
 return &resources_[r.internalIndex];
}
FfxErrorCode GpuBackend::Create(const FfxCreateResourceDescription& desc,unsigned owner,FfxResourceInternal& out){
 if(!Ready())return FFX_ERROR_BACKEND_API_ERROR;
 Resource r;r.desc=desc.resourceDescription;r.owner=owner;r.alive=true;
 if(r.desc.type==FFX_RESOURCE_TYPE_BUFFER){r.memory=allocate(r.desc.size);}
 else {
  auto fmt=format(r.desc.format);if(fmt==DkImageFormat_None||!r.desc.width||!r.desc.height)return fail("unsupported image format/dimensions");
  if(!r.desc.mipCount){unsigned n=std::max(r.desc.width,r.desc.height);r.desc.mipCount=1;while(n>1){n>>=1;r.desc.mipCount++;}}
  DkImageLayoutMaker maker;dkImageLayoutMakerDefaults(&maker,device_);maker.format=fmt;maker.dimensions[0]=r.desc.width;maker.dimensions[1]=r.desc.height;maker.dimensions[2]=1;maker.mipLevels=r.desc.mipCount;
  maker.type=r.desc.type==FFX_RESOURCE_TYPE_TEXTURE1D?DkImageType_1D:DkImageType_2D;
  maker.flags=DkImageFlags_UsageLoadStore|DkImageFlags_UsageRender;
  DkImageLayout layout;dkImageLayoutInitialize(&layout,&maker);uint64_t size=dkImageLayoutGetSize(&layout);
  if(size>256*1024*1024)return fail("image allocation exceeds limit");
  r.memory=allocate(unsigned(size),DkMemBlockFlags_Image);if(!r.memory)return FFX_ERROR_BACKEND_API_ERROR;
  dkImageInitialize(&r.image,&layout,r.memory,0);
 }
 if(!r.memory)return FFX_ERROR_BACKEND_API_ERROR;
 if(desc.initData.type==FFX_RESOURCE_INIT_DATA_TYPE_VALUE)std::memset(dkMemBlockGetCpuAddr(r.memory),desc.initData.value,dkMemBlockGetSize(r.memory));
 else if(desc.initData.type==FFX_RESOURCE_INIT_DATA_TYPE_BUFFER){
  // Current SDK uses a single RG8 texel for its default distortion field.
  // Arbitrary linear texture initializers require a staging copy, not memcpy.
  if(r.desc.type!=FFX_RESOURCE_TYPE_BUFFER&&(r.desc.width!=1||r.desc.height!=1)){dkMemBlockDestroy(r.memory);return fail("nontrivial texture initialization not supported");}
  if(!desc.initData.buffer||desc.initData.size>dkMemBlockGetSize(r.memory)){dkMemBlockDestroy(r.memory);return fail("invalid resource initializer");}
  std::memcpy(dkMemBlockGetCpuAddr(r.memory),desc.initData.buffer,desc.initData.size);
 }
 out.internalIndex=int(resources_.size());resources_.push_back(r);
 // SDK reserves +1 for initialization upload resource destruction.
 resources_.emplace_back();return FFX_OK;
}
FfxErrorCode GpuBackend::Destroy(FfxResourceInternal id,unsigned){
 if(id.internalIndex==0)return FFX_OK;
 if(id.internalIndex<0||unsigned(id.internalIndex)>=resources_.size())return fail("destroy resource out of range");
 // Keep allocations alive until the queue and backend are destroyed. SDK may
 // retire aliases while already recorded commands still reference their pages.
 resources_[id.internalIndex].alive=false;return FFX_OK;
}
FfxErrorCode GpuBackend::Register(const FfxResource& external,unsigned,FfxResourceInternal& out){
 if(!external.resource){out.internalIndex=0;return FFX_OK;}
 for(unsigned i=1;i<resources_.size();i++)if(external.resource==&resources_[i]&&resources_[i].alive){out.internalIndex=i;return FFX_OK;}
 return fail("external resource belongs to another device");
}
FfxResource GpuBackend::External(FfxResourceInternal id){
 FfxResource out{};auto r=resource(id);if(r){out.resource=r;out.description=r->desc;out.state=FFX_RESOURCE_STATE_UNORDERED_ACCESS;}return out;
}
FfxResourceDescription GpuBackend::Describe(FfxResourceInternal id){auto r=resource(id);return r?r->desc:FfxResourceDescription{};}
FfxResource GpuBackend::Shared(const FfxCreateResourceDescription& desc){FfxResourceInternal id{};return Create(desc,0,id)==FFX_OK?External(id):FfxResource{};}
FfxErrorCode GpuBackend::ReleaseContext(unsigned owner){for(auto& r:resources_)if(r.owner==owner)r.alive=false;return FFX_OK;}
DkGpuAddr GpuBackend::stage(const void* data,unsigned size,unsigned alignment){
 unsigned offset=(scratchOffset_+alignment-1)&~(alignment-1);
 if(!size||size>ScratchBytes||offset>ScratchBytes-size){fail("GPU per-submission scratch exhausted");return DK_GPU_ADDR_INVALID;}
 if(data)std::memcpy((char*)dkMemBlockGetCpuAddr(scratch_)+offset,data,size);
 scratchOffset_=offset+size;return dkMemBlockGetGpuAddr(scratch_)+offset;
}
FfxErrorCode GpuBackend::Pipeline(const NxFfxPass& meta,const FfxPipelineDescription&,unsigned){
 if(programs_.count(meta.file))return FFX_OK;
 std::string path=shaderRoot_+"/"+meta.file+".dksh";FILE* file=std::fopen(path.c_str(),"rb");if(!file)return fail("native shader missing");
 uint32_t h[6];bool valid=std::fread(h,sizeof h,1,file)==1&&h[0]==0x48534b44&&h[1]==24&&h[5]==1&&h[2]>=24&&h[2]<65536&&h[3]>0&&h[3]<4*1024*1024;
 if(!valid){std::fclose(file);return fail("invalid native shader header");}
 void* control=std::malloc(h[2]);if(!control){std::fclose(file);return fail("shader control allocation failed");}
 std::rewind(file);valid=std::fread(control,h[2],1,file)==1;
 Program p;p.code=allocate(h[3]+DK_SHADER_CODE_UNUSABLE_SIZE,DkMemBlockFlags_Code);
 valid=valid&&p.code&&std::fread(dkMemBlockGetCpuAddr(p.code),h[3],1,file)==1&&std::fgetc(file)==EOF;std::fclose(file);
 if(valid){DkShaderMaker maker;dkShaderMakerDefaults(&maker,p.code,0);maker.control=control;dkShaderInitialize(&p.shader,&maker);valid=dkShaderIsValid(&p.shader)&&dkShaderGetStage(&p.shader)==DkStage_Compute;}
 std::free(control);if(!valid){if(p.code)dkMemBlockDestroy(p.code);return fail("invalid/truncated native compute shader");}
 programs_.emplace(meta.file,p);return FFX_OK;
}
FfxErrorCode GpuBackend::dispatch(const FfxComputeJobDescription& job){
 const auto& p=job.pipeline;auto meta=(const NxFfxPass*)p.pipeline;
 if(!meta||!programs_.count(meta->file))return fail("unknown compute pipeline");
 dkCmdBufBarrier(commands_,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors|DkInvalidateFlags_L2Cache);
 const DkShader* shader=&programs_.at(meta->file).shader;dkCmdBufBindShaders(commands_,DkStageFlag_Compute,&shader,1);
 DkImageDescriptor descriptors[64]{};unsigned descriptorCount=0;
 struct ImageBinding{unsigned slot;DkResHandle handle;bool storage;};ImageBinding handles[64];unsigned handleCount=0;
 Resource* packed=nullptr;
 if(meta->packedPyramid){
  int index=binding(p.uavTextureBindings,p.uavTextureCount,"rw_inpainting_pyramid0");if(index<0)return fail("packed pyramid resource missing");
  packed=resource(job.uavTextures[index].resource);if(!packed)return FFX_ERROR_BACKEND_API_ERROR;
  if(!packed->packed){if(!packed->pyramid.Initialize(packed->desc.width,packed->desc.height,packed->desc.mipCount))return fail("invalid packed pyramid layout");packed->packed=allocate(packed->pyramid.bytes);if(!packed->packed)return FFX_ERROR_BACKEND_API_ERROR;}
 }
 for(unsigned i=0;i<meta->count;i++){
  auto& r=meta->bindings[i];int index=-1;
  if(!strcmp(r.name,"nxPyramid")){
   DkBufExtents span{dkMemBlockGetGpuAddr(packed->packed),packed->pyramid.bytes};dkCmdBufBindStorageBuffers(commands_,DkStage_Compute,r.slot,&span,1);continue;
  }
  if(!strcmp(r.name,"nxPyramidLayout")){
   auto address=stage(packed->pyramid.levels,sizeof packed->pyramid.levels);if(address==DK_GPU_ADDR_INVALID)return FFX_ERROR_BACKEND_API_ERROR;
   DkBufExtents span{address,sizeof packed->pyramid.levels};dkCmdBufBindUniformBuffers(commands_,DkStage_Compute,r.slot,&span,1);continue;
  }
  if(!strcmp(r.kind,"uniform")){
   index=binding(p.constantBufferBindings,p.constCount,r.name);if(index<0)return fail("constant binding missing");
   auto& cb=job.cbs[index];unsigned bytes=cb.num32BitEntries*4;auto addr=stage(cb.data,bytes);if(addr==DK_GPU_ADDR_INVALID)return FFX_ERROR_BACKEND_API_ERROR;
   DkBufExtents span{addr,bytes};dkCmdBufBindUniformBuffers(commands_,DkStage_Compute,r.slot,&span,1);
  } else if(!strcmp(r.kind,"texture")||!strcmp(r.kind,"image")){
   bool storage=!strcmp(r.kind,"image");index=binding(storage?p.uavTextureBindings:p.srvTextureBindings,storage?p.uavTextureCount:p.srvTextureCount,r.name);
   if(index<0)return fail("image/texture binding missing");
   Resource* target=resource(storage?job.uavTextures[index].resource:job.srvTextures[index].resource);if(!target)return FFX_ERROR_BACKEND_API_ERROR;
   unsigned mip=storage?std::min(job.uavTextures[index].mip,target->desc.mipCount-1):0;auto v=view(target->image,mip);
   if(descriptorCount>=64||handleCount>=64)return fail("too many image descriptors");
   dkImageDescriptorInitialize(&descriptors[descriptorCount],&v,storage,false);
   unsigned sampler=strstr(r.sampler,"Linear")||strstr(r.sampler,"linear")?1:0;
   handles[handleCount++]={r.slot,storage?dkMakeImageHandle(descriptorCount):dkMakeTextureHandle(descriptorCount,sampler),storage};descriptorCount++;
  } else if(!strcmp(r.kind,"storage")){
   bool writable=!strncmp(r.name,"rw_",3);index=binding(writable?p.uavBufferBindings:p.srvBufferBindings,writable?p.uavBufferCount:p.srvBufferCount,r.name);if(index<0)return fail("SSBO binding missing");
   Resource* target=resource(writable?job.uavBuffers[index].resource:job.srvBuffers[index].resource);if(!target)return FFX_ERROR_BACKEND_API_ERROR;
   unsigned offset=writable?job.uavBuffers[index].offset:job.srvBuffers[index].offset;
   unsigned bytes=writable?job.uavBuffers[index].size:job.srvBuffers[index].size;if(!bytes)bytes=target->desc.size-offset;
   if(offset>target->desc.size||bytes>target->desc.size-offset)return fail("SSBO range out of bounds");
   DkBufExtents span{dkMemBlockGetGpuAddr(target->memory)+offset,bytes};dkCmdBufBindStorageBuffers(commands_,DkStage_Compute,r.slot,&span,1);
  } else return fail("unknown binding class");
 }
 if(descriptorCount){auto address=stage(descriptors,descriptorCount*sizeof(DkImageDescriptor),32);if(address==DK_GPU_ADDR_INVALID)return FFX_ERROR_BACKEND_API_ERROR;dkCmdBufBindImageDescriptorSet(commands_,address,descriptorCount);}
 dkCmdBufBindSamplerDescriptorSet(commands_,dkMemBlockGetGpuAddr(samplers_),2);
 for(unsigned i=0;i<handleCount;i++){auto& h=handles[i];if(h.storage)dkCmdBufBindImages(commands_,DkStage_Compute,h.slot,&h.handle,1);else dkCmdBufBindTextures(commands_,DkStage_Compute,h.slot,&h.handle,1);}
 dkCmdBufDispatchCompute(commands_,job.dimensions[0],job.dimensions[1],job.dimensions[2]);
 if(packed){
  dkCmdBufBarrier(commands_,DkBarrier_Full,DkInvalidateFlags_L2Cache);
  for(unsigned mip=0;mip<packed->pyramid.count;mip++){
   auto& level=packed->pyramid.levels[mip];auto v=view(packed->image,mip);
   DkCopyBuf source{dkMemBlockGetGpuAddr(packed->packed)+uint64_t(level.offset)*8,0,0};DkImageRect rect{0,0,0,level.width,level.height,1};
   dkCmdBufCopyBufferToImage(commands_,&source,&v,&rect,0);
  }
  dkCmdBufBarrier(commands_,DkBarrier_Full,DkInvalidateFlags_Image);
 }
 return FFX_OK;
}
FfxErrorCode GpuBackend::clear(const FfxClearFloatJobDescription& job){
 auto r=resource(job.target);if(!r)return FFX_ERROR_BACKEND_API_ERROR;
 if(r->desc.type==FFX_RESOURCE_TYPE_BUFFER){
  uint32_t words[64];for(unsigned i=0;i<64;i++)std::memcpy(words+i,job.color+(i%4),4);
  for(unsigned offset=0;offset<r->desc.size;offset+=sizeof words)dkCmdBufPushData(commands_,dkMemBlockGetGpuAddr(r->memory)+offset,words,std::min(unsigned(sizeof words),r->desc.size-offset));
 }else{
  for(unsigned mip=0;mip<r->desc.mipCount;mip++){auto v=view(r->image,mip);const DkImageView* ptr=&v;dkCmdBufBindRenderTargets(commands_,&ptr,1,nullptr);dkCmdBufClearColor(commands_,0,0xf,job.color);}
 }
 return FFX_OK;
}
FfxErrorCode GpuBackend::copy(const FfxCopyJobDescription& job){
 auto src=resource(job.src),dst=resource(job.dst);if(!src||!dst)return FFX_ERROR_BACKEND_API_ERROR;
 if(src->desc.type==FFX_RESOURCE_TYPE_BUFFER&&dst->desc.type==FFX_RESOURCE_TYPE_BUFFER){
  unsigned bytes=job.size?job.size:std::min(src->desc.size,dst->desc.size);
  if(job.srcOffset>src->desc.size||job.dstOffset>dst->desc.size||bytes>src->desc.size-job.srcOffset||bytes>dst->desc.size-job.dstOffset)return fail("buffer copy out of bounds");
  dkCmdBufCopyBuffer(commands_,dkMemBlockGetGpuAddr(src->memory)+job.srcOffset,dkMemBlockGetGpuAddr(dst->memory)+job.dstOffset,bytes);
 }else if(src->desc.type==FFX_RESOURCE_TYPE_TEXTURE2D&&dst->desc.type==FFX_RESOURCE_TYPE_TEXTURE2D){
  if(src->desc.width!=dst->desc.width||src->desc.height!=dst->desc.height)return fail("texture copy size mismatch");
  auto sv=view(src->image),dv=view(dst->image);DkImageRect rect{0,0,0,src->desc.width,src->desc.height,1};dkCmdBufCopyImage(commands_,&sv,&rect,&dv,&rect,0);
 }else return fail("unsupported resource copy");return FFX_OK;
}
FfxErrorCode GpuBackend::Schedule(const FfxGpuJobDescription& job){
 if(!Ready())return FFX_ERROR_BACKEND_API_ERROR;
 dkCmdBufBarrier(commands_,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);
 FfxErrorCode result=FFX_OK;
 switch(job.jobType){
 case FFX_GPU_JOB_COMPUTE:
  lastPass_=((NxFfxPass*)job.computeJobDescriptor.pipeline.pipeline)->file;
  if(traceJobs_&&logger_)logger_(lastPass_);
  result=dispatch(job.computeJobDescriptor);break;
 case FFX_GPU_JOB_CLEAR_FLOAT:result=clear(job.clearJobDescriptor);break;
 case FFX_GPU_JOB_COPY:result=copy(job.copyJobDescriptor);break;
 case FFX_GPU_JOB_BARRIER:case FFX_GPU_JOB_DISCARD:break;
 default:return fail("unsupported SDK GPU job");
 }
 if(result==FFX_OK&&traceJobs_)return Execute(nullptr,0);
 return result;
}
FfxErrorCode GpuBackend::Execute(FfxCommandList,unsigned){
 if(!Ready())return FFX_ERROR_BACKEND_API_ERROR;
 uint64_t started=armGetSystemTick();
 DkFence fence;dkCmdBufBarrier(commands_,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);dkCmdBufSignalFence(commands_,&fence,true);
 dkQueueSubmitCommands(queue_,dkCmdBufFinishList(commands_));dkQueueFlush(queue_);
 if(dkFenceWait(&fence,2000000000LL)!=DkResult_Success||dkQueueIsInErrorState(queue_)){quarantined_=true;return fail("SDK compute GPU fence failed");}
 if(traceJobs_&&logger_){char line[256];std::snprintf(line,sizeof line,"submit+wait pass=%s %.3fms (diagnostic serialization)",lastPass_,double(armGetSystemTick()-started)*1000/19200000.0);logger_(line);}
 dkCmdBufClear(commands_);scratchOffset_=0;return FFX_OK;
}
FfxErrorCode GpuBackend::Upload(FfxResource target,DkGpuAddr source,bool submit){
 FfxResourceInternal id{};if(Register(target,0,id)!=FFX_OK)return FFX_ERROR_BACKEND_API_ERROR;auto r=resource(id);if(!r)return FFX_ERROR_BACKEND_API_ERROR;
 auto v=view(r->image);DkCopyBuf buffer{source,0,0};DkImageRect rect{0,0,0,r->desc.width,r->desc.height,1};dkCmdBufCopyBufferToImage(commands_,&buffer,&v,&rect,0);return submit?Execute(nullptr,0):FFX_OK;
}
FfxErrorCode GpuBackend::Download(FfxResource source,DkGpuAddr target){
 FfxResourceInternal id{};if(Register(source,0,id)!=FFX_OK)return FFX_ERROR_BACKEND_API_ERROR;auto r=resource(id);if(!r)return FFX_ERROR_BACKEND_API_ERROR;
 auto v=view(r->image);DkCopyBuf buffer{target,0,0};DkImageRect rect{0,0,0,r->desc.width,r->desc.height,1};dkCmdBufCopyImageToBuffer(commands_,&v,&rect,&buffer,0);return Execute(nullptr,0);
}
}
