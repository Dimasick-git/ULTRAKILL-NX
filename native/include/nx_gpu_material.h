#pragma once
#include "nx_material.h"
#include <deko3d.h>
namespace nx::game {
// Initial native base-texture pass. Original lighting, animated shader features,
// transparency ordering and effect passes remain separate migration work.
class GpuMaterials {
 DkDevice device{};DkQueue queue{};DkMemBlock imagesMemory{},descriptors{},samplers{},uniforms{};
 std::vector<DkImage> images;std::vector<uint32_t> chunkBindings;
 std::vector<DkResHandle> mainHandles,emissiveHandles;
 uint32_t count=0;
 DkMemBlock allocate(uint32_t,uint32_t);
public:
 ~GpuMaterials();
 bool initialize(DkDevice,DkQueue,const MaterialPack&,const RenderPack&,std::string&);
 void bindSet(DkCmdBuf)const;
 void bind(DkCmdBuf,uint32_t material)const;
 uint32_t materialForChunk(uint32_t chunk)const{return chunkBindings[chunk];}
};
}
