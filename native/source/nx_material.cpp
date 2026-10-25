#include "nx_material.h"
#include <map>
#include <set>
#include <utility>
namespace nx::game {
bool MaterialPack::load(const char*path,std::string&error){FILE*f=std::fopen(path,"rb");if(!f){error="cannot open material pack";return false;}MaterialPack candidate;bool ok=candidate.read(f,error);std::fclose(f);if(ok)*this=std::move(candidate);return ok;}
bool MaterialPack::read(FILE*f,std::string&error){
 error="invalid material pack header";
 if(std::fread(&header,sizeof header,1,f)!=1||std::memcmp(header.magic,"NXMAT\0\0\0",8)||header.version!=1||!header.materialCount||header.materialCount>65536||header.textureCount>65536||header.pixelBytes>256*1024*1024)return false;
 uint64_t bytes=uint64_t(header.materialCount)*sizeof(MaterialRecord)+uint64_t(header.textureCount)*sizeof(TextureRecord)+header.pixelBytes;
 long start=std::ftell(f);if(start<0||std::fseek(f,0,SEEK_END)||std::ftell(f)<start||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;
 materials.resize(header.materialCount);textures.resize(header.textureCount);pixels.resize(header.pixelBytes);error="truncated material payload";
 if(std::fread(materials.data(),sizeof(MaterialRecord),materials.size(),f)!=materials.size()||std::fread(textures.data(),sizeof(TextureRecord),textures.size(),f)!=textures.size()||std::fread(pixels.data(),1,pixels.size(),f)!=pixels.size())return false;
 uint32_t crc=crc32(materials.data(),materials.size()*sizeof(MaterialRecord));crc=crc32(textures.data(),textures.size()*sizeof(TextureRecord),crc);crc=crc32(pixels.data(),pixels.size(),crc);
 if(crc!=header.crc){error="material checksum mismatch";return false;}return validate(error);
}
bool MaterialPack::validate(std::string&error)const{
 error="invalid material configuration/counts";
 if(header.version!=1||std::memcmp(header.magic,"NXMAT\0\0\0",8)||materials.empty()||materials.size()>65536||textures.size()>65536||pixels.size()>256*1024*1024||header.materialCount!=materials.size()||header.textureCount!=textures.size()||header.pixelBytes!=pixels.size())return false;
 std::set<std::pair<uint32_t,int64_t>> identities;
 for(const auto&m:materials){
  if(m.file>65535||(!m.id&&(m.file||m.shader))||!identities.emplace(m.file,m.id).second||m.shader>4||m.flags&~3u||m.reserved||(m.mainTexture!=NoTexture&&m.mainTexture>=textures.size())||(m.emissiveTexture!=NoTexture&&m.emissiveTexture>=textures.size()))return false;
  for(const auto*values:{m.uv,m.tint,m.emissive,m.parameters,m.emissiveUv})for(unsigned i=0;i<4;i++)if(!std::isfinite(values[i]))return false;
 }
 error="invalid texture layout/sampler";uint64_t next=0;std::set<std::pair<uint32_t,int64_t>> textureIds;
 for(const auto&t:textures){
  if(!t.width||t.width>8192||!t.height||t.height>8192||t.offset!=next||uint64_t(t.width)*t.height*4!=t.bytes||uint64_t(t.offset)+t.bytes>pixels.size()||t.wrapU>3||t.wrapV>3||t.filter>2||t.colorSpace>1||!t.sourceId||t.flags||!textureIds.emplace(t.sourceFileCrc,t.sourceId).second)return false;
  for(auto r:t.reserved)if(r)return false;next+=t.bytes;
 }if(next!=pixels.size())return false;error.clear();return true;
}
bool MaterialPack::bind(const RenderPack&render,std::vector<uint32_t>&output,std::string&error)const{
 if(!validate(error)||!render.validate(error))return false;
 if(header.renderCrc!=render.header.crc){error="materials belong to a different render pack";return false;}
 std::map<std::pair<uint32_t,int64_t>,uint32_t> lookup;
 for(uint32_t i=0;i<materials.size();i++)lookup.emplace(std::make_pair(materials[i].file,materials[i].id),i);
 std::vector<uint32_t> candidate;candidate.reserve(render.chunks.size());
 for(const auto&c:render.chunks){auto found=lookup.find({c.materialFile,c.materialId});if(found==lookup.end()){error="original chunk material missing";return false;}candidate.push_back(found->second);}
 output=std::move(candidate);error.clear();return true;
}
}
