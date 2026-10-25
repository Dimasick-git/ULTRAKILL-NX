#include "nx_actor.h"
#include <cfloat>
#include <utility>
namespace nx::game {
uint32_t physicsChecksum(const PhysicsPack&p){uint32_t crc=crc32(&p.player,sizeof p.player);crc=crc32(p.layerMasks.data(),sizeof p.layerMasks,crc);crc=crc32(p.colliders.data(),p.colliders.size()*sizeof(ColliderRecord),crc);crc=crc32(p.vertices.data(),p.vertices.size()*sizeof(Vec3),crc);return crc32(p.indices.data(),p.indices.size()*4,crc);}
bool ActorPack::load(const char*path,std::string&error){FILE*f=std::fopen(path,"rb");if(!f){error="cannot open actor pack";return false;}ActorPack candidate;bool ok=candidate.read(f,error);std::fclose(f);if(ok)*this=std::move(candidate);return ok;}
bool ActorPack::read(FILE*f,std::string&error){
 error="invalid actor pack header";
 if(std::fread(&header,sizeof header,1,f)!=1||std::memcmp(header.magic,"NXACTOR\0",8)||header.version!=1||!header.bodyCount||header.bodyCount>16384||header.colliderCount>65536||header.reserved[0]||header.reserved[1])return false;
 uint64_t bytes=uint64_t(header.bodyCount)*sizeof(ActorRecord)+uint64_t(header.colliderCount)*sizeof(ActorColliderRef);long start=std::ftell(f);if(start<0||std::fseek(f,0,SEEK_END)||std::ftell(f)<start||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;
 bodies.resize(header.bodyCount);colliders.resize(header.colliderCount);if(std::fread(bodies.data(),sizeof(ActorRecord),bodies.size(),f)!=bodies.size()||std::fread(colliders.data(),sizeof(ActorColliderRef),colliders.size(),f)!=colliders.size())return false;
 uint32_t crc=crc32(bodies.data(),bodies.size()*sizeof(ActorRecord));crc=crc32(colliders.data(),colliders.size()*sizeof(ActorColliderRef),crc);if(crc!=header.crc){error="actor checksum mismatch";return false;}return validate(error);
}
bool ActorPack::validate(std::string&error)const{
 error="invalid actor counts/configuration";
 if(header.version!=1||std::memcmp(header.magic,"NXACTOR\0",8)||bodies.empty()||bodies.size()>16384||colliders.size()>65536||header.bodyCount!=bodies.size()||header.colliderCount!=colliders.size()||header.reserved[0]||header.reserved[1])return false;
 size_t next=0;std::unordered_set<int64_t> ids,objects;std::unordered_set<uint32_t> referenced;
 for(const auto&b:bodies){
  if(!b.id||!b.objectId||!ids.insert(b.id).second||!objects.insert(b.objectId).second||b.flags&~127u||b.layer>=32||b.constraints&~126u||b.ccd>3||b.interpolation>2||b.reserved||b.firstCollider!=next||uint64_t(b.firstCollider)+b.colliderCount>colliders.size())return false;
  if(!finite(b.position)||!finite(b.center)||!finite(b.inertia)||!std::isfinite(b.mass)||b.mass<=0||!std::isfinite(b.drag)||b.drag<0||!std::isfinite(b.angularDrag)||b.angularDrag<0)return false;
  for(const float*q:{b.rotation,b.inertiaRotation}){float length=0;for(unsigned i=0;i<4;i++){if(!std::isfinite(q[i]))return false;length+=q[i]*q[i];}if(std::abs(length-1)>.001f)return false;}
  if(!(b.flags&ActorImplicitTensor)&&(b.inertia.x<=0||b.inertia.y<=0||b.inertia.z<=0))return false;
  for(size_t i=b.firstCollider;i<size_t(b.firstCollider)+b.colliderCount;i++)if(colliders[i].relativeActive>1||!referenced.insert(colliders[i].index).second)return false;next+=b.colliderCount;
 }if(next!=colliders.size())return false;error.clear();return true;
}
bool ActorPack::bind(const PhysicsPack&p,const GraphPack&definitions,const WorldGraph&graph,std::string&error)const{
 if(!validate(error)||!definitions.validate(error))return false;const char*physicsError=nullptr;if(!p.validate(&physicsError)){error=physicsError;return false;}
 if(header.graphCrc!=definitions.checksum()||header.physicsCrc!=physicsChecksum(p)){error="actor graph/physics identity mismatch";return false;}
 size_t expected=0;for(const auto&c:definitions.components)expected+=definitions.text(c.typeOffset,c.typeLength)=="Rigidbody";if(expected!=bodies.size()){error="actor pack omits original rigidbody components";return false;}
 std::unordered_set<uint32_t> referenced;unsigned players=0;
 for(const auto&b:bodies){
  auto node=graph.find(b.objectId),component=graph.findComponent(b.id);if(node==WorldGraph::Missing||component==WorldGraph::Missing||definitions.components[component].node!=node||definitions.text(definitions.components[component].typeOffset,definitions.components[component].typeLength)!="Rigidbody"){error="rigidbody component ownership missing";return false;}
  const auto*n=graph.node(node);Vec3 position{n->world.m[12],n->world.m[13],n->world.m[14]};
  // Hidden authored ragdolls can sit tens of thousands of units away. The
  // exporter composes in double; runtime matrices compose in float. Compare
  // with an absolute floor and a small ULP-scaled allowance, not a fixed mm.
  float compositionScale=0;int32_t ancestor=int32_t(node);while(ancestor>=0){const auto*state=graph.node(uint32_t(ancestor));compositionScale+=std::max({std::abs(state->world.m[12]),std::abs(state->world.m[13]),std::abs(state->world.m[14]),std::abs(state->local.position.x),std::abs(state->local.position.y),std::abs(state->local.position.z)});ancestor=state->local.parent;}
  auto same=[&](float x,float y){return std::abs(x-y)<=.001f+16*FLT_EPSILON*std::max({std::abs(x),std::abs(y),compositionScale});};
  if(!same(position.x,b.position.x)||!same(position.y,b.position.y)||!same(position.z,b.position.z)||n->local.layer!=b.layer||n->active!=bool(b.flags&ActorActive)){error="authored actor transform/layer/activity mismatch: body "+std::to_string(b.id)+", squared position difference "+std::to_string(dot(position-b.position,position-b.position))+", layer "+std::to_string(n->local.layer)+"/"+std::to_string(b.layer)+", active "+std::to_string(n->active)+"/"+std::to_string(bool(b.flags&ActorActive));return false;}
  if(b.flags&ActorPlayer){players++;if(b.objectId!=p.player.objectId){error="player actor binding mismatch";return false;}}
  for(size_t i=b.firstCollider;i<size_t(b.firstCollider)+b.colliderCount;i++){
   auto index=colliders[i].index;if(index>=p.colliders.size()||p.colliders[index].bodyId!=b.id||!(p.colliders[index].flags&AttachedBody)){error="actor collider ownership mismatch";return false;}referenced.insert(index);
   auto at=graph.find(p.colliders[index].objectId);bool relative=true;while(at!=node){const auto*child=graph.node(at);if(!child||child->local.parent<0){error="actor collider is not a body descendant";return false;}relative=relative&&bool(child->local.flags&1);at=uint32_t(child->local.parent);}
   if(relative!=bool(colliders[i].relativeActive)||bool(p.colliders[index].flags&Active)!=(relative&&bool(b.flags&ActorActive))){error="actor relative collider activity mismatch";return false;}
  }
 }
 if(players!=1){error="actor pack needs exactly one original player binding";return false;}
 for(uint32_t i=0;i<p.colliders.size();i++)if(bool(p.colliders[i].flags&AttachedBody)!=bool(referenced.count(i))){error="actor pack omits an authored body collider";return false;}
 error.clear();return true;
}
}
