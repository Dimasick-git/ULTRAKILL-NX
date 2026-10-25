#pragma once
#include "nx_scene.h"
#include <array>
#include <unordered_set>

namespace nx::game {
enum ColliderKind:uint32_t {Box=1,Mesh=2,Sphere=3,Capsule=4};
enum ColliderFlags:uint32_t {Active=1,Enabled=2,Trigger=4,AttachedBody=8,Convex=16};
enum GroundTags:uint32_t {FloorTag=1,SlipperyTag=2,MovingTag=4};
enum SurfaceFlags:uint32_t {HasSurface=1,PushSurface=2,CanJump=4,SilentJumpFail=8,CanWallJump=16,CanSlide=32,SilentSlideFail=64,CanDash=128,SilentDashFail=256,CanClimbStep=512,Launchable=1024,ForceCrouch=2048,OverrideFootsteps=4096,OverrideSurface=8192,DontRotateCamera=16384};
struct PlayerSpec {
 int64_t objectId=0;
 Vec3 position{},up{},forward{},cameraOffset{};
 float walkSpeed=0,airAcceleration=0,jumpPower=0,wallJumpPower=0,mass=0,radius=0,height=0;
 Vec3 center{},gravity{};
 float dynamicFriction=0,staticFriction=0,restitution=0;
 uint32_t frictionCombine=0,bounceCombine=0;
 int64_t groundCollider=0,slopeCollider=0;
};
struct ColliderRecord {
 int64_t id=0,objectId=0,bodyId=0;
 uint32_t kind=0,layer=0,flags=0,firstVertex=0,vertexCount=0,firstIndex=0,indexCount=0,groundTags=0;
 float world[12]{},center[3]{},size[3]{},radius=0,height=0;
 float dynamicFriction=0,staticFriction=0,restitution=0;
 uint32_t frictionCombine=0,bounceCombine=0,tailReserved=0;
 int64_t surfaceId=0;float surfaceFriction=1,surfaceSpeed=1,surfacePush[3]{},surfaceJump=1;uint32_t surfaceFlags=0,surfaceReserved=0;
 Vec3 point(Vec3 p)const{return {world[0]*p.x+world[1]*p.y+world[2]*p.z+world[3],world[4]*p.x+world[5]*p.y+world[6]*p.z+world[7],world[8]*p.x+world[9]*p.y+world[10]*p.z+world[11]};}
};
struct PhysicsHeader {char magic[8];uint32_t version,colliderCount,vertexCount,indexCount,crc,reserved;};
static_assert(sizeof(PlayerSpec)==144 && sizeof(ColliderRecord)==200 && sizeof(PhysicsHeader)==32,"physics pack ABI");
inline bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct PhysicsPack {
 PlayerSpec player;
 std::array<uint32_t,32> layerMasks{};
 std::vector<ColliderRecord> colliders;
 std::vector<Vec3> vertices;
 std::vector<uint32_t> indices;
 bool load(const char*path,const char**error){
  *error="cannot open physics pack";FILE*f=std::fopen(path,"rb");if(!f)return false;
  PhysicsPack candidate;bool success=candidate.read(f,error);std::fclose(f);if(success)*this=std::move(candidate);return success;
 }
 bool read(FILE*f,const char**error){
  PhysicsHeader h{};*error="invalid physics header";
  if(std::fread(&h,sizeof h,1,f)!=1||std::memcmp(h.magic,"NXPHYS\0\0",8)||h.version!=3||h.reserved)return false;
  if(!h.colliderCount||h.colliderCount>65536||h.vertexCount>4000000||h.indexCount>12000000||h.indexCount%3)return false;
  uint64_t bytes=sizeof(PlayerSpec)+sizeof(layerMasks)+uint64_t(h.colliderCount)*sizeof(ColliderRecord)+uint64_t(h.vertexCount)*sizeof(Vec3)+uint64_t(h.indexCount)*4;
  long start=std::ftell(f);if(start<0||std::fseek(f,0,SEEK_END)||std::ftell(f)<0||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;
  colliders.resize(h.colliderCount);vertices.resize(h.vertexCount);indices.resize(h.indexCount);
  *error="truncated physics payload";
  if(std::fread(&player,sizeof player,1,f)!=1||std::fread(layerMasks.data(),sizeof layerMasks,1,f)!=1||std::fread(colliders.data(),sizeof(ColliderRecord),colliders.size(),f)!=colliders.size()||std::fread(vertices.data(),sizeof(Vec3),vertices.size(),f)!=vertices.size()||std::fread(indices.data(),4,indices.size(),f)!=indices.size())return false;
  uint32_t crc=crc32(&player,sizeof player);crc=crc32(layerMasks.data(),sizeof layerMasks,crc);crc=crc32(colliders.data(),colliders.size()*sizeof(ColliderRecord),crc);crc=crc32(vertices.data(),vertices.size()*sizeof(Vec3),crc);crc=crc32(indices.data(),indices.size()*4,crc);
  *error="physics checksum mismatch";if(crc!=h.crc)return false;
  return validate(error);
 }
 bool validate(const char**error)const{
  *error="invalid player physics configuration";
  if(!player.objectId||!finite(player.position)||!finite(player.up)||!finite(player.forward)||!finite(player.center)||!finite(player.cameraOffset)||!finite(player.gravity))return false;
  if(std::abs(dot(player.up,player.up)-1)>.001f||std::abs(dot(player.forward,player.forward)-1)>.001f||std::abs(dot(player.up,player.forward))>.001f)return false;
  for(float v:{player.walkSpeed,player.airAcceleration,player.jumpPower,player.wallJumpPower,player.mass,player.radius,player.height})if(!std::isfinite(v)||v<=0)return false;
  if(player.height<player.radius*2||!materialValid(player.dynamicFriction,player.staticFriction,player.restitution,player.frictionCombine,player.bounceCombine))return false;
  *error="asymmetric physics layer matrix";
  for(unsigned a=0;a<32;a++)for(unsigned b=0;b<32;b++)if(bool(layerMasks[a]&(1u<<b))!=bool(layerMasks[b]&(1u<<a)))return false;
  *error="invalid collision geometry";
  for(auto v:vertices)if(!finite(v))return false;
  for(auto v:indices)if(v>=vertices.size())return false;
  std::unordered_set<int64_t> ids;
  for(const auto&c:colliders){
   if(!c.id||!c.objectId||!ids.insert(c.id).second||c.layer>=32||c.kind<Box||c.kind>Capsule||c.flags&~31u||c.groundTags&~7u||c.tailReserved||bool(c.bodyId)!=bool(c.flags&AttachedBody))return false;
   if(c.surfaceFlags&~32767u||c.surfaceReserved||bool(c.surfaceId)!=bool(c.surfaceFlags&HasSurface))return false;
   for(float v:{c.surfaceFriction,c.surfaceSpeed,c.surfacePush[0],c.surfacePush[1],c.surfacePush[2],c.surfaceJump})if(!std::isfinite(v))return false;
   for(float v:c.world)if(!std::isfinite(v))return false;
   for(float v:c.center)if(!std::isfinite(v))return false;
   for(float v:c.size)if(!std::isfinite(v))return false;
   if(!std::isfinite(c.radius)||!std::isfinite(c.height)||!materialValid(c.dynamicFriction,c.staticFriction,c.restitution,c.frictionCombine,c.bounceCombine))return false;
   if(c.kind==Mesh){
    if(c.firstVertex>vertices.size()||c.vertexCount>vertices.size()-c.firstVertex||c.firstIndex>indices.size()||c.indexCount>indices.size()-c.firstIndex||c.indexCount%3)return false;
    for(uint32_t i=0;i<c.indexCount;i++){auto index=indices[c.firstIndex+i];if(index<c.firstVertex||index-c.firstVertex>=c.vertexCount)return false;}
   }else if(c.firstVertex||c.vertexCount||c.firstIndex||c.indexCount)return false;
   if(c.kind==Box)for(float v:c.size)if(v<0)return false;
   if((c.kind==Sphere||c.kind==Capsule)&&c.radius<0)return false;
   if(c.kind==Capsule&&(c.height<0||c.size[0]<0||c.size[0]>2||c.size[0]!=std::floor(c.size[0])))return false;
  }
  *error="invalid ground sensor reference";
  for(auto id:{player.groundCollider,player.slopeCollider})if(id){bool found=false;for(const auto&c:colliders)if(c.id==id){found=c.kind==Capsule && bool(c.flags&Trigger);break;}if(!found)return false;}
  *error=nullptr;return true;
 }
 static bool materialValid(float dynamic,float stat,float bounce,uint32_t frictionMode,uint32_t bounceMode){return std::isfinite(dynamic)&&std::isfinite(stat)&&std::isfinite(bounce)&&dynamic>=0&&stat>=0&&bounce>=0&&bounce<=1&&frictionMode<=3&&bounceMode<=3;}
};
}
