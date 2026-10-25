#pragma once
#include "nx_world.h"
#include "nx_physics_pack.h"
namespace nx::game {
enum ActorFlags:uint32_t {ActorActive=1,ActorKinematic=2,ActorGravity=4,ActorImplicitCenter=8,ActorImplicitTensor=16,ActorPlayer=32,ActorHasJoint=64};
struct ActorHeader {char magic[8];uint32_t version,bodyCount,colliderCount,graphCrc,physicsCrc,crc,reserved[2];};
struct ActorRecord {
 int64_t id,objectId;uint32_t flags,layer,constraints,ccd,firstCollider,colliderCount,interpolation,reserved;
 Vec3 position;float rotation[4],mass,drag,angularDrag;Vec3 center,inertia;float inertiaRotation[4];
};
struct ActorPose {Vec3 position{},velocity{},angularVelocity{};float rotation[4]{0,0,0,1};};
struct ActorColliderRef {uint32_t index,relativeActive;};
static_assert(sizeof(ActorHeader)==40&&sizeof(ActorRecord)==128&&sizeof(ActorColliderRef)==8,"actor pack ABI");
uint32_t physicsChecksum(const PhysicsPack&);
struct ActorPack {
 ActorHeader header{};std::vector<ActorRecord> bodies;std::vector<ActorColliderRef> colliders;
 bool load(const char*,std::string&);
 bool read(FILE*,std::string&);
 bool validate(std::string&)const;
 bool bind(const PhysicsPack&,const GraphPack&,const WorldGraph&,std::string&)const;
};
}
