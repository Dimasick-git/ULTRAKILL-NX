#pragma once
#include "nx_physics_pack.h"
#include "nx_movement.h"
#include "nx_actor.h"
#include "nx_joint.h"
#include <memory>
#include <string>
namespace nx::game {
struct PhysicsCoverage {unsigned staticBodies=0,sensors=0,dynamicDeferred=0,inactive=0,emptyMesh=0,unsupported=0,actorBodies=0,actorColliders=0,activeJoints=0;};
struct RayHit {bool hit=false;int64_t collider=0;Vec3 point{},normal{};float distance=0;};
enum class ActorForceMode:uint32_t {Force=0,Impulse=1,VelocityChange=2,Acceleration=5};
// Initial native solver adapter. It deliberately does not report deferred
// authored dynamic bodies or gameplay trigger callbacks as implemented.
class PhysicsWorld {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 PhysicsWorld();~PhysicsWorld();
 PhysicsWorld(const PhysicsWorld&)=delete;PhysicsWorld&operator=(const PhysicsWorld&)=delete;
 bool initialize(const PhysicsPack&,unsigned workers,std::string&error);
 bool initializeActors(const ActorPack&,const GraphPack&,const WorldGraph&,std::string&);
 bool initializeJoints(const JointPack&,const ActorPack&,const GraphPack&,std::string&);
 bool setJointEnabled(int64_t,bool,std::string&);
 bool jointState(int64_t,NativeJointState&,std::string&)const;
 const std::vector<int64_t>&jointBreakEvents()const;
 void clearJointBreakEvents();
 bool setActorKinematic(int64_t,bool,std::string&);
 bool setActorMass(int64_t,float,std::string&);
 float actorMass(int64_t)const;
 bool setActorGravity(int64_t,bool,std::string&);
 bool actorUsesGravity(int64_t)const;
 // Center-of-mass force modes accumulate until the next fixed tick. Immediate
 // velocity writes do not discard them; disable/motion-mode changes do.
 bool addActorForce(int64_t,Vec3,ActorForceMode,std::string&);
 bool setActorActive(int64_t body,bool active,std::string&);
 bool actorActive(int64_t body)const;
 bool actorKinematic(int64_t body)const;
 bool hasActor(int64_t body)const;
 const std::vector<int64_t>&activeActorIds()const;
 bool actorPose(int64_t body,ActorPose&,std::string&)const;
 bool setActorPose(int64_t body,const ActorPose&,std::string&);
 bool moveActorKinematic(int64_t body,Vec3 position,const float rotation[4],float deltaTime,std::string&);
 PhysicsCoverage coverage()const;
 bool step(float seconds,std::string&error);
 Vec3 playerPosition()const;Vec3 playerVelocity()const;
 void setPlayerVelocity(Vec3);
 bool setPlayerLayer(unsigned layer,std::string&error);
 unsigned playerLayer()const;
 bool setLayerCollision(unsigned a,unsigned b,bool enabled,std::string&error);
 // Static authored bodies are created lazily and retained when disabled.
 // Runtime dynamic bodies and transform changes use separate adapters.
 bool setStaticColliderActive(int64_t collider,bool active,std::string&error);
 bool setStaticColliderState(int64_t collider,bool hierarchyActive,bool enabled,std::string&error);
 bool staticColliderActive(int64_t collider)const;
 const ColliderRecord*colliderMetadata(int64_t collider)const;
 const ColliderRecord*colliderAt(uint32_t index)const;
 // Preserve authored mass and locked rotation while changing capsule geometry.
 // groundOffset is world-space and applies to GroundCheck, not SlopeCheck.
 bool setPlayerCapsule(float height,Vec3 positionDelta,Vec3 groundOffset,std::string&error);
 float playerHeight()const;
 Vec3 playerCapsuleBottom()const;
 Vec3 groundProbePosition(bool slope=false)const;
 void applyMovement(const MovementResult&);
 RayHit ray(Vec3 start,Vec3 displacement,uint32_t layerMask=~0u,bool includeTriggers=false)const;
 bool sphereCast(Vec3 start,float radius,Vec3 displacement,uint32_t layerMask,bool includeTriggers,RayHit&,std::string&error)const;
 bool overlapCapsule(Vec3 endpointA,Vec3 endpointB,float radius,uint32_t layerMask,bool includeTriggers,std::vector<int64_t>&colliders,std::string&error)const;
 // Raw overlaps only: GroundCheck tag/portal/state callbacks are a separate
 // gameplay layer. forceUnscaled reproduces ForceGroundCheck's distinct shape.
 bool groundOverlaps(bool slope,bool forceUnscaled,std::vector<int64_t>&colliders,std::string&error)const;
};
}
