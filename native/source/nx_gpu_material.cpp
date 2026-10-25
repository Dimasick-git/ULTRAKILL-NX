#include "nx_gpu_material.h"
namespace nx::game {
namespace {uint32_t aligned(uint32_t n,uint32_t a){return (n+a-1)&~(a-1);}}
DkMemBlock GpuMaterials::allocate(uint32_t bytes,uint32_t flags){DkMemBlockMaker maker;dkMemBlockMakerDefaults(&maker,device,aligned(bytes,DK_MEMBLOCK_ALIGNMENT));maker.flags=flags;return dkMemBlockCreate(&maker);}
GpuMaterials::~GpuMaterials(){if(queue)dkQueueWaitIdle(queue);for(auto block:{uniforms,samplers,descriptors,imagesMemory})if(block)dkMemBlockDestroy(block);}
bool GpuMaterials::initialize(DkDevice d,DkQueue q,const MaterialPack&p,const RenderPack&r,std::string&error){
 if(device){error="GPU material set already initialized";return false;}if(!p.bind(r,chunkBindings,error))return false;device=d;queue=q;count=p.textures.size()+2;
 constexpr uint32_t dataFlags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached;
 std::vector<DkImageLayout> layouts(count);std::vector<uint32_t> offsets(count);uint64_t total=0;
 for(uint32_t i=0;i<count;i++){
  DkImageLayoutMaker maker;dkImageLayoutMakerDefaults(&maker,device);maker.dimensions[0]=i<p.textures.size()?p.textures[i].width:1;maker.dimensions[1]=i<p.textures.size()?p.textures[i].height:1;maker.format=DkImageFormat_RGBA8_Unorm;
  dkImageLayoutInitialize(&layouts[i],&maker);total=aligned(total,dkImageLayoutGetAlignment(&layouts[i]));offsets[i]=total;total+=dkImageLayoutGetSize(&layouts[i]);if(total>320*1024*1024){error="GPU texture allocation budget";return false;}
 }
 imagesMemory=allocate(total,DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);descriptors=allocate(count*sizeof(DkImageDescriptor),dataFlags);samplers=allocate(count*sizeof(DkSamplerDescriptor),dataFlags);uniforms=allocate(p.materials.size()*DK_UNIFORM_BUF_ALIGNMENT,dataFlags);
 if(!imagesMemory||!descriptors||!samplers||!uniforms){error="GPU material memory allocation failed";return false;}
 images.resize(count);auto imageDescriptors=static_cast<DkImageDescriptor*>(dkMemBlockGetCpuAddr(descriptors));auto samplerDescriptors=static_cast<DkSamplerDescriptor*>(dkMemBlockGetCpuAddr(samplers));
 const DkWrapMode wrap[]={DkWrapMode_Repeat,DkWrapMode_ClampToEdge,DkWrapMode_MirroredRepeat,DkWrapMode_MirrorClampToEdge};
 for(uint32_t i=0;i<count;i++){
  dkImageInitialize(&images[i],&layouts[i],imagesMemory,offsets[i]);DkImageView view;dkImageViewDefaults(&view,&images[i]);dkImageDescriptorInitialize(&imageDescriptors[i],&view,false,false);
  DkSampler sampler;dkSamplerDefaults(&sampler);if(i<p.textures.size()){const auto&t=p.textures[i];sampler.minFilter=sampler.magFilter=t.filter?DkFilter_Linear:DkFilter_Nearest;sampler.wrapMode[0]=wrap[t.wrapU];sampler.wrapMode[1]=wrap[t.wrapV];}dkSamplerDescriptorInitialize(&samplerDescriptors[i],&sampler);
 }
 mainHandles.reserve(p.materials.size());emissiveHandles.reserve(p.materials.size());
 for(uint32_t i=0;i<p.materials.size();i++){
  const auto&m=p.materials[i];uint32_t main=m.mainTexture==MaterialPack::NoTexture?count-2:m.mainTexture,emissive=m.emissiveTexture==MaterialPack::NoTexture?count-1:m.emissiveTexture;
  mainHandles.push_back(dkMakeTextureHandle(main,main));emissiveHandles.push_back(dkMakeTextureHandle(emissive,emissive));
  std::memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(uniforms))+i*DK_UNIFORM_BUF_ALIGNMENT,m.uv,80);
 }
 DkMemBlock upload=allocate(p.pixels.size()+8,dataFlags),commands=allocate(1024*1024,dataFlags);
 if(!upload||!commands){if(upload)dkMemBlockDestroy(upload);if(commands)dkMemBlockDestroy(commands);error="GPU texture staging allocation failed";return false;}
 std::memcpy(dkMemBlockGetCpuAddr(upload),p.pixels.data(),p.pixels.size());uint32_t fallback[]={0xffffffffu,0xff000000u};std::memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(upload))+p.pixels.size(),fallback,8);
 DkCmdBufMaker cm;dkCmdBufMakerDefaults(&cm,device);DkCmdBuf cb=dkCmdBufCreate(&cm);if(!cb){dkMemBlockDestroy(commands);dkMemBlockDestroy(upload);error="texture upload command buffer failed";return false;}dkCmdBufAddMemory(cb,commands,0,1024*1024);
 for(uint32_t i=0;i<count;i++){
  bool original=i<p.textures.size();DkImageView view;dkImageViewDefaults(&view,&images[i]);DkImageRect rect{0,0,0,original?p.textures[i].width:1,original?p.textures[i].height:1,1};DkCopyBuf source{dkMemBlockGetGpuAddr(upload)+(original?p.textures[i].offset:p.pixels.size()+4*(i-p.textures.size())),0,0};dkCmdBufCopyBufferToImage(cb,&source,&view,&rect,0);
 }
 dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);DkFence fence;dkCmdBufSignalFence(cb,&fence,true);dkQueueSubmitCommands(queue,dkCmdBufFinishList(cb));dkQueueFlush(queue);DkResult result=dkFenceWait(&fence,5000000000LL);
 // Even on a failed bounded fence, do not free staging still referenced by DMA.
 dkQueueWaitIdle(queue);dkCmdBufDestroy(cb);dkMemBlockDestroy(commands);dkMemBlockDestroy(upload);
 if(result!=DkResult_Success||dkQueueIsInErrorState(queue)){error="native texture upload fence/queue failure";return false;}error.clear();return true;
}
void GpuMaterials::bindSet(DkCmdBuf cb)const{dkCmdBufBindImageDescriptorSet(cb,dkMemBlockGetGpuAddr(descriptors),count);dkCmdBufBindSamplerDescriptorSet(cb,dkMemBlockGetGpuAddr(samplers),count);}
void GpuMaterials::bind(DkCmdBuf cb,uint32_t material)const{DkResHandle handles[]={mainHandles[material],emissiveHandles[material]};dkCmdBufBindTextures(cb,DkStage_Fragment,0,handles,2);dkCmdBufBindUniformBuffer(cb,DkStage_Fragment,0,dkMemBlockGetGpuAddr(uniforms)+material*DK_UNIFORM_BUF_ALIGNMENT,DK_UNIFORM_BUF_ALIGNMENT);}
}
