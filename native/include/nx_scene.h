#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>

namespace nx {
struct Vec3 { float x,y,z; };
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 normalized(Vec3 a){float n=std::sqrt(dot(a,a));return n>1e-8f?a*(1/n):Vec3{0,0,0};}
struct Mat4 { float m[16]{}; };
inline Mat4 multiply(const Mat4&a,const Mat4&b){Mat4 c;for(int col=0;col<4;col++)for(int row=0;row<4;row++)for(int k=0;k<4;k++)c.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];return c;}
struct Camera {
 Vec3 position{0,2,0};float yaw=0,pitch=0,fov=75;
 Vec3 forward()const{return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};}
 Vec3 right()const{return {std::cos(yaw),0,-std::sin(yaw)};}
 Mat4 matrix(float aspect)const{
  Vec3 f=forward(),r=right(),u=cross(f,r);Mat4 view;
  view.m[0]=r.x;view.m[4]=r.y;view.m[8]=r.z;view.m[12]=-dot(r,position);
  view.m[1]=u.x;view.m[5]=u.y;view.m[9]=u.z;view.m[13]=-dot(u,position);
  view.m[2]=f.x;view.m[6]=f.y;view.m[10]=f.z;view.m[14]=-dot(f,position);view.m[15]=1;
  const float near=.05f,far=2000.f,t=1/std::tan(fov*3.14159265359f/360.f);Mat4 proj;
  proj.m[0]=t/aspect;proj.m[5]=t;proj.m[10]=far/(far-near);proj.m[11]=1;proj.m[14]=-near*far/(far-near);
  return multiply(proj,view);
 }
};
struct Vertex {float position[3],normal[3],uv[2];uint32_t color;};
struct Chunk {uint32_t firstIndex,indexCount;float minimum[3],maximum[3];};
struct Header {char magic[8];uint32_t version,vertexCount,indexCount,chunkCount;float spawn[3],yaw,pitch,fov;uint32_t payloadCrc,reserved;};
static_assert(sizeof(Vertex)==36 && sizeof(Chunk)==32 && sizeof(Header)==56,"scene ABI");
inline uint32_t crc32(const void*data,size_t length,uint32_t crc=0){crc=~crc;const auto*p=(const uint8_t*)data;while(length--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;}
inline bool visible(const Chunk&chunk,const Mat4&matrix){
 unsigned outside=63;
 for(unsigned corner=0;corner<8;corner++){
  float x=(corner&1)?chunk.maximum[0]:chunk.minimum[0],y=(corner&2)?chunk.maximum[1]:chunk.minimum[1],z=(corner&4)?chunk.maximum[2]:chunk.minimum[2];
  float q[4];for(int i=0;i<4;i++)q[i]=matrix.m[i]*x+matrix.m[4+i]*y+matrix.m[8+i]*z+matrix.m[12+i];
  unsigned mask=(q[0]<-q[3]?1:0)|(q[0]>q[3]?2:0)|(q[1]<-q[3]?4:0)|(q[1]>q[3]?8:0)|(q[2]<0?16:0)|(q[2]>q[3]?32:0);
  outside&=mask;
 }
 return outside==0;
}
struct Scene {
 Header header{};std::vector<Vertex> vertices;std::vector<uint32_t> indices;std::vector<Chunk> chunks;
 bool load(const char*path,const char**error){
  *error="cannot open scene";FILE*f=std::fopen(path,"rb");if(!f)return false;
  bool ok=read(f,error);std::fclose(f);if(!ok){vertices.clear();indices.clear();chunks.clear();}return ok;
 }
 bool read(FILE*f,const char**error){
  *error="invalid scene header";
  if(std::fread(&header,sizeof header,1,f)!=1||std::memcmp(header.magic,"NXSCENE\0",8)||header.version!=1||header.reserved)return false;
  // Explicit budgets before allocations; a corrupt pack cannot exhaust app memory.
  if(!header.vertexCount||header.vertexCount>4000000||!header.indexCount||header.indexCount>12000000||header.indexCount%3||!header.chunkCount||header.chunkCount>65536)return false;
  for(float v:header.spawn)if(!std::isfinite(v))return false;
  if(!std::isfinite(header.yaw)||!std::isfinite(header.pitch)||!std::isfinite(header.fov)||header.fov<20||header.fov>140)return false;
  const uint64_t bytes=uint64_t(header.vertexCount)*sizeof(Vertex)+uint64_t(header.indexCount)*4+uint64_t(header.chunkCount)*sizeof(Chunk);
  const long start=std::ftell(f);if(std::fseek(f,0,SEEK_END)||std::ftell(f)<0||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;
  vertices.resize(header.vertexCount);indices.resize(header.indexCount);chunks.resize(header.chunkCount);
  *error="truncated scene payload";
  if(std::fread(vertices.data(),sizeof(Vertex),vertices.size(),f)!=vertices.size()||std::fread(indices.data(),4,indices.size(),f)!=indices.size()||std::fread(chunks.data(),sizeof(Chunk),chunks.size(),f)!=chunks.size())return false;
  uint32_t crc=crc32(vertices.data(),vertices.size()*sizeof(Vertex));crc=crc32(indices.data(),indices.size()*4,crc);crc=crc32(chunks.data(),chunks.size()*sizeof(Chunk),crc);
  *error="scene checksum mismatch";if(crc!=header.payloadCrc)return false;
  *error="invalid scene geometry";
  for(const auto&v:vertices){for(float f:v.position)if(!std::isfinite(f))return false;for(float f:v.normal)if(!std::isfinite(f))return false;for(float f:v.uv)if(!std::isfinite(f))return false;}
  for(auto i:indices)if(i>=vertices.size())return false;
  uint32_t next=0;
  for(const auto&c:chunks){if(c.firstIndex!=next||!c.indexCount||c.indexCount%3||c.indexCount>indices.size()-next)return false;for(int j=0;j<3;j++)if(!std::isfinite(c.minimum[j])||!std::isfinite(c.maximum[j])||c.minimum[j]>c.maximum[j])return false;next+=c.indexCount;}
  if(next!=indices.size())return false;
  // Validate the culling contract against actual vertices, not just range sizes.
  for(const auto&c:chunks)for(uint32_t i=0;i<c.indexCount;i++)for(int j=0;j<3;j++){float p=vertices[indices[c.firstIndex+i]].position[j];if(p<c.minimum[j]-.01f||p>c.maximum[j]+.01f)return false;}
  *error=nullptr;return true;
 }
 Camera camera()const{return {{header.spawn[0],header.spawn[1],header.spawn[2]},header.yaw,header.pitch,header.fov};}
};
}
