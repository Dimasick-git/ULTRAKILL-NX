#pragma once
#include "nx_world.h"
namespace nx::game {
struct RenderHeader {char magic[8];uint32_t version,vertexCount,indexCount,chunkCount,ownerCount,cameraMask;float spawn[3],yaw,pitch,fov;uint32_t crc,graphCrc;};
struct RenderOwner {int64_t rendererId,objectId;uint32_t firstChunk,chunkCount,flags,reserved;};
struct RenderChunk {uint32_t owner,materialFile;int64_t materialId;uint32_t firstIndex,indexCount;float minimum[3],maximum[3];};
static_assert(sizeof(RenderHeader)==64&&sizeof(RenderOwner)==32&&sizeof(RenderChunk)==48,"render pack ABI");
struct RenderPack {
 RenderHeader header{};std::vector<Vertex> vertices;std::vector<uint32_t> indices;std::vector<RenderOwner> owners;std::vector<RenderChunk> chunks;
 bool load(const char*,std::string&);
 bool read(FILE*,std::string&);
 bool validate(std::string&)const;
 Camera camera()const{return {{header.spawn[0],header.spawn[1],header.spawn[2]},header.yaw,header.pitch,header.fov};}
};
struct ModelUniform {Mat4 modelViewProjection,normalWorld;};
static_assert(sizeof(ModelUniform)==128,"draw uniform ABI");
struct RenderDraw {uint32_t chunk,owner;};
struct RenderBinding {uint32_t node=WorldGraph::Missing,component=WorldGraph::Missing;Mat4 previousWorld{},normalWorld{};bool cached=false,degenerate=false;};
// CPU draw selection shared by host checks and deko3d. Cached bounds/normals
// update only when an owner's transform changes. Material references remain in
// each draw; shader/texture parity and dynamic/skinned geometry are separate.
class RenderPlan {
 const RenderPack*pack=nullptr;const WorldGraph*graph=nullptr;
 std::vector<RenderBinding> bindings;
 std::vector<Chunk> worldBounds;
 std::vector<ModelUniform> uniforms;
 std::vector<RenderDraw> draws;
 bool update(uint32_t,const Mat4&,std::string&);
public:
 bool initialize(const RenderPack&,const GraphPack&,const WorldGraph&,std::string&);
 bool prepare(const Camera&,float aspect,std::string&);
 const std::vector<RenderDraw>& drawList()const{return draws;}
 const ModelUniform& uniform(uint32_t owner)const{return uniforms[owner];}
 const RenderBinding*binding(uint32_t owner)const{return owner<bindings.size()?&bindings[owner]:nullptr;}
 const Chunk* bounds(uint32_t chunk)const{return chunk<worldBounds.size()?&worldBounds[chunk]:nullptr;}
};
}
