#pragma once
#include "nx_ground.h"
#include "nx_physics.h"
#include "nx_jump.h"
#include "nx_dash.h"
#include "nx_crouch.h"
#include "nx_world.h"
#include "nx_actor_scene.h"
#include "nx_activation.h"
#include <unordered_map>
#include <string>
#include <algorithm>
namespace nx::game {
// First integrated native movement path. Weapon/enemy/scene-script systems are
// not yet connected; this remains an internal development session.
class NativeSession final:private GroundBridge {
 const PhysicsPack&definitions;
 const GraphPack*graphDefinitions=nullptr;
 const ActorPack*actorDefinitions=nullptr;ActorScene actorScene;
 const JointPack*jointDefinitions=nullptr;
 std::unique_ptr<SceneActivation> activation;
 WorldGraph hierarchy;uint32_t playerNode=WorldGraph::Missing,groundNode=WorldGraph::Missing;
 PhysicsWorld world;
 GroundState ground,slope;
 GroundPlayerState groundPlayer;
 MovementState movement;
 JumpState jumping;
 DashState dashing;
 CrouchState posture;Vec3 groundOffset{};float slideSafety=0;
 bool jumpPending=false,jumpHeld=false,dashPending=false,slidePending=false,slideHeld=false,slideCanceled=false;
 double slidePressedAt=0;
 double jumpPressedAt=0;
 std::vector<JumpEvent> effects;
 std::vector<DashEvent> dashEffects;
 std::unordered_map<int64_t,const ColliderRecord*> byCollider,bySurface;
 std::vector<int64_t> contacts,previousContacts;
 Vec3 previousPosition{};
 double gameTime=0;
 std::string pendingEvent;
 const ColliderRecord*collider(int64_t)const override;
 bool tracked(int64_t)const override;
 void attach(int64_t)override;
 void detach(int64_t)override;
 void shockwave(float)override;
 void bounce(GroundState&,Vec3,GroundPlayerState&)override;
 bool observe(GroundState&,bool,std::string&);
public:
 explicit NativeSession(const PhysicsPack&pack,const GraphPack*graph=nullptr,const ActorPack*actors=nullptr,const JointPack*joints=nullptr):definitions(pack),graphDefinitions(graph),actorDefinitions(actors),jointDefinitions(joints){}
 bool initialize(unsigned workers,std::string&error);
 bool tick(float moveX,float moveY,float yaw,float deltaTime,std::string&error);
 // A render frame can contain no physics tick. Keep its button edge until
 // exactly one tick consumes it; focus/pause transitions discard stale edges.
 bool requestJump(double timestamp){if(!std::isfinite(timestamp)||timestamp<0)return false;if(!jumpPending){jumpPending=true;jumpPressedAt=timestamp;}return true;}
 void requestDash(){dashPending=true;}
 bool requestSlide(double timestamp){if(!std::isfinite(timestamp)||timestamp<0)return false;if(!slidePending){slidePending=true;slidePressedAt=timestamp;}return true;}
 void setSlideHeld(bool value){if(slideHeld&&!value)slideCanceled=true;slideHeld=value;}
 void setJumpHeld(bool held){jumpHeld=held;}
 void clearPendingInput(){jumpPending=false;jumpHeld=false;dashPending=false;slidePending=false;slideCanceled=slideCanceled||slideHeld;slideHeld=false;}
 const std::vector<JumpEvent>& effectRequests()const{return effects;}
 const std::vector<DashEvent>& dashEffectRequests()const{return dashEffects;}
 void clearEffectRequests(){effects.clear();dashEffects.clear();}
 const JumpState& jumpState()const{return jumping;}
 const DashState& dashState()const{return dashing;}
 unsigned collisionLayer()const{return world.playerLayer();}
 float capsuleHeight()const{return world.playerHeight();}
 const CrouchState& crouchState()const{return posture;}
 const WorldGraph*sceneGraph()const{return graphDefinitions?&hierarchy:nullptr;}
 Vec3 position()const{return world.playerPosition();}
 Vec3 velocity()const{return world.playerVelocity();}
 Vec3 cameraPosition(float alpha=1)const{return previousPosition+(position()-previousPosition)*std::max(0.f,std::min(1.f,alpha))+definitions.player.cameraOffset+posture.cameraTargetOffset;}
 PhysicsCoverage coverage()const{return world.coverage();}
 const SceneActivation*sceneActivation()const{return activation.get();}
 const std::vector<int64_t>&jointBreakRequests()const{return world.jointBreakEvents();}
 void clearJointBreakRequests(){world.clearJointBreakEvents();}
 bool grounded()const{return ground.onGround;}
};
}
