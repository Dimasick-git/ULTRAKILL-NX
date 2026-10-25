#pragma once
#include "nx_render.h"
namespace nx::game {
struct MaterialHeader {char magic[8];uint32_t version,materialCount,textureCount,pixelBytes,crc,renderCrc;};
struct MaterialRecord {
 uint32_t file,shader;int64_t id;uint32_t mainTexture,emissiveTexture,flags,reserved;
 float uv[4],tint[4],emissive[4],parameters[4],emissiveUv[4];
};
struct TextureRecord {
 uint32_t width,height,offset,bytes,wrapU,wrapV,filter,colorSpace;int64_t sourceId;
 uint32_t sourceFileCrc,flags,reserved[4];
};
static_assert(sizeof(MaterialHeader)==32&&sizeof(MaterialRecord)==112&&sizeof(TextureRecord)==64,"material pack ABI");
class MaterialPack {
public:
 static constexpr uint32_t NoTexture=UINT32_MAX;
 MaterialHeader header{};std::vector<MaterialRecord> materials;std::vector<TextureRecord> textures;std::vector<uint8_t> pixels;
 bool load(const char*,std::string&);
 bool read(FILE*,std::string&);
 bool validate(std::string&)const;
 bool bind(const RenderPack&,std::vector<uint32_t>&chunkMaterials,std::string&)const;
};
}
