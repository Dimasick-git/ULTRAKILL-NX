#include "nx_render.h"
#include <cfloat>
#include <utility>
namespace nx::game {
bool RenderPack::load(const char*path,std::string&error){FILE*f=std::fopen(path,"rb");if(!f){error="cannot open owned render pack";return false;}RenderPack candidate;bool ok=candidate.read(f,error);std::fclose(f);if(ok)*this=std::move(candidate);return ok;}
bool RenderPack::read(FILE*f,std::string&error){
 error="invalid render pack header";
 if(std::fread(&header,sizeof header,1,f)!=1||std::memcmp(header.magic,"NXRENDER",8)||header.version!=1||!header.vertexCount||header.vertexCount>4000000||!header.indexCount||header.indexCount>12000000||header.indexCount%3||!header.ownerCount||header.ownerCount>65536||!header.chunkCount||header.chunkCount>262144)return false;
 uint64_t bytes=uint64_t(header.vertexCount)*sizeof(Vertex)+uint64_t(header.indexCount)*4+uint64_t(header.ownerCount)*sizeof(RenderOwner)+uint64_t(header.chunkCount)*sizeof(RenderChunk);
 long start=std::ftell(f);if(start<0||std::fseek(f,0,SEEK_END)||std::ftell(f)<0||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;
 vertices.resize(header.vertexCount);indices.resize(header.indexCount);owners.resize(header.ownerCount);chunks.resize(header.chunkCount);error="truncated render payload";
 if(std::fread(vertices.data(),sizeof(Vertex),vertices.size(),f)!=vertices.size()||std::fread(indices.data(),4,indices.size(),f)!=indices.size()||std::fread(owners.data(),sizeof(RenderOwner),owners.size(),f)!=owners.size()||std::fread(chunks.data(),sizeof(RenderChunk),chunks.size(),f)!=chunks.size())return false;
 uint32_t crc=crc32(vertices.data(),vertices.size()*sizeof(Vertex));crc=crc32(indices.data(),indices.size()*4,crc);crc=crc32(owners.data(),owners.size()*sizeof(RenderOwner),crc);crc=crc32(chunks.data(),chunks.size()*sizeof(RenderChunk),crc);
 if(crc!=header.crc){error="owned render checksum mismatch";return false;}return validate(error);
}
bool RenderPack::validate(std::string&error)const{
 error="render counts/configuration mismatch";
 if(header.version!=1||vertices.empty()||vertices.size()>4000000||indices.empty()||indices.size()>12000000||indices.size()%3||owners.empty()||owners.size()>65536||chunks.empty()||chunks.size()>262144||header.vertexCount!=vertices.size()||header.indexCount!=indices.size()||header.ownerCount!=owners.size()||header.chunkCount!=chunks.size())return false;
 for(float v:header.spawn)if(!std::isfinite(v))return false;if(!std::isfinite(header.yaw)||!std::isfinite(header.pitch)||!std::isfinite(header.fov)||header.fov<20||header.fov>140)return false;
 for(const auto&v:vertices){for(float f:v.position)if(!std::isfinite(f))return false;for(float f:v.normal)if(!std::isfinite(f))return false;for(float f:v.uv)if(!std::isfinite(f))return false;}
 for(auto i:indices)if(i>=vertices.size())return false;
 error="invalid render ownership";std::unordered_set<int64_t> renderers;size_t nextChunk=0;
 for(size_t o=0;o<owners.size();o++){
  const auto&owner=owners[o];if(!owner.rendererId||!owner.objectId||!renderers.insert(owner.rendererId).second||owner.reserved||owner.flags&~7u||owner.firstChunk!=nextChunk||!owner.chunkCount||uint64_t(owner.firstChunk)+owner.chunkCount>chunks.size())return false;
  for(size_t c=owner.firstChunk;c<size_t(owner.firstChunk)+owner.chunkCount;c++)if(chunks[c].owner!=o)return false;nextChunk+=owner.chunkCount;
 }if(nextChunk!=chunks.size())return false;
 error="invalid owned geometry ranges/bounds";size_t nextIndex=0;
 for(const auto&c:chunks){
  if(c.owner>=owners.size()||c.materialFile>65535||c.firstIndex!=nextIndex||!c.indexCount||c.indexCount%3||uint64_t(c.firstIndex)+c.indexCount>indices.size())return false;
  for(unsigned axis=0;axis<3;axis++)if(!std::isfinite(c.minimum[axis])||!std::isfinite(c.maximum[axis])||c.minimum[axis]>c.maximum[axis])return false;
  for(size_t i=c.firstIndex;i<size_t(c.firstIndex)+c.indexCount;i++)for(unsigned a=0;a<3;a++){float v=vertices[indices[i]].position[a];if(v<c.minimum[a]-.01f||v>c.maximum[a]+.01f)return false;}
  nextIndex+=c.indexCount;
 }if(nextIndex!=indices.size())return false;error.clear();return true;
}
bool RenderPlan::initialize(const RenderPack&source,const GraphPack&hierarchy,const WorldGraph&world,std::string&error){
 if(!source.validate(error)||!hierarchy.validate(error))return false;
 if(source.header.graphCrc!=hierarchy.checksum()){error="render pack belongs to a different authored hierarchy";return false;}
 std::vector<RenderBinding> next(source.owners.size());
 for(size_t i=0;i<source.owners.size();i++){
  const auto&o=source.owners[i];uint32_t component=world.findComponent(o.rendererId),node=world.find(o.objectId);
  if(component==WorldGraph::Missing||node==WorldGraph::Missing||component>=hierarchy.components.size()||hierarchy.components[component].node!=node||hierarchy.text(hierarchy.components[component].typeOffset,hierarchy.components[component].typeLength)!="MeshRenderer"){error="render owner/component binding missing";return false;}
  next[i].node=node;next[i].component=component;
 }
 pack=&source;graph=&world;bindings=std::move(next);worldBounds.resize(source.chunks.size());uniforms.resize(source.owners.size());draws.clear();draws.reserve(source.chunks.size());error.clear();return true;
}
bool RenderPlan::update(uint32_t owner,const Mat4&m,std::string&error){
 auto&b=bindings[owner];if(b.cached&&!std::memcmp(b.previousWorld.m,m.m,sizeof m.m))return true;
 float a=m.m[0],c=m.m[8],d=m.m[1],e=m.m[5],f=m.m[9],g=m.m[2],h=m.m[6],i=m.m[10],x=m.m[4];
 float determinant=a*(e*i-f*h)-x*(d*i-f*g)+c*(d*h-e*g);b.degenerate=std::abs(determinant)<1e-12f;
 if(!std::isfinite(determinant)){error="render normal transform overflow";return false;}
 if(b.degenerate){b.cached=true;b.previousWorld=m;return true;}
 Mat4 n;n.m[0]=(e*i-f*h)/determinant;n.m[1]=(c*h-x*i)/determinant;n.m[2]=(x*f-c*e)/determinant;n.m[4]=(f*g-d*i)/determinant;n.m[5]=(a*i-c*g)/determinant;n.m[6]=(c*d-a*f)/determinant;n.m[8]=(d*h-e*g)/determinant;n.m[9]=(x*g-a*h)/determinant;n.m[10]=(a*e-x*d)/determinant;n.m[15]=1;
 const auto&o=pack->owners[owner];
 for(size_t index=o.firstChunk;index<size_t(o.firstChunk)+o.chunkCount;index++){
  const auto&source=pack->chunks[index];Chunk bounds{source.firstIndex,source.indexCount,{FLT_MAX,FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX,-FLT_MAX}};
  for(unsigned corner=0;corner<8;corner++){
   float p[3];for(unsigned axis=0;axis<3;axis++)p[axis]=(corner&(1u<<axis))?source.maximum[axis]:source.minimum[axis];
   for(unsigned row=0;row<3;row++){float value=m.m[row]*p[0]+m.m[4+row]*p[1]+m.m[8+row]*p[2]+m.m[12+row];if(!std::isfinite(value)){error="render bounds transform overflow";return false;}bounds.minimum[row]=std::min(bounds.minimum[row],value);bounds.maximum[row]=std::max(bounds.maximum[row],value);}
  }worldBounds[index]=bounds;
 }
 b.normalWorld=n;b.previousWorld=m;b.cached=true;return true;
}
bool RenderPlan::prepare(const Camera&camera,float aspect,std::string&error){
 if(!pack||!graph||!std::isfinite(aspect)||aspect<=0||!std::isfinite(camera.position.x)||!std::isfinite(camera.position.y)||!std::isfinite(camera.position.z)||!std::isfinite(camera.yaw)||!std::isfinite(camera.pitch)||!std::isfinite(camera.fov)||camera.fov<20||camera.fov>140){error="invalid native render camera";return false;}
 draws.clear();auto vp=camera.matrix(aspect);
 for(uint32_t owner=0;owner<bindings.size();owner++){
  const auto&b=bindings[owner];const auto*node=graph->node(b.node);
  if(!node||!graph->componentActive(b.component)||(pack->owners[owner].flags&3)==3||!(pack->header.cameraMask&(1u<<node->local.layer)))continue;
  if(!update(owner,node->world,error))return false;if(bindings[owner].degenerate)continue;
  bool uniformWritten=false;const auto&o=pack->owners[owner];
  for(size_t chunk=o.firstChunk;chunk<size_t(o.firstChunk)+o.chunkCount;chunk++)if(visible(worldBounds[chunk],vp)){
   if(!uniformWritten){uniforms[owner]={multiply(vp,node->world),bindings[owner].normalWorld};uniformWritten=true;}draws.push_back({uint32_t(chunk),owner});
  }
 }error.clear();return true;
}
}
