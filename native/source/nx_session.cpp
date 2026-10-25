#include "nx_session.h"
#include <algorithm>
#include <cmath>
namespace nx::game {
const ColliderRecord*NativeSession::collider(int64_t id)const{return world.colliderMetadata(id);}
bool NativeSession::tracked(int64_t)const{return false;}
void NativeSession::attach(int64_t){pendingEvent="moving-support attachment requires native parenting system";}
void NativeSession::detach(int64_t){pendingEvent="moving-support detachment requires native parenting system";}
void NativeSession::shockwave(float){pendingEvent="shockwave requires native damage/effects system";}
void NativeSession::bounce(GroundState&,Vec3,GroundPlayerState&){pendingEvent="enemy bounce requires native jump system";}
bool NativeSession::initialize(unsigned workers,std::string&error){
 if(!definitions.player.groundCollider||!definitions.player.slopeCollider){error="player ground sensor bindings missing";return false;}
 if(!world.initialize(definitions,workers,error))return false;
 byCollider.clear();bySurface.clear();for(const auto&c:definitions.colliders){byCollider.emplace(c.id,&c);if(c.surfaceId)bySurface.emplace(c.surfaceId,&c);}
 if(graphDefinitions){
  if(!hierarchy.initialize(*graphDefinitions,error))return false;
  playerNode=hierarchy.find(definitions.player.objectId);const auto*sensor=collider(definitions.player.groundCollider);groundNode=sensor?hierarchy.find(sensor->objectId):WorldGraph::Missing;
  if(playerNode==WorldGraph::Missing||groundNode==WorldGraph::Missing){error="authored player/ground hierarchy binding missing";return false;}
  const auto&matrix=hierarchy.node(playerNode)->world;Vec3 origin{matrix.m[12],matrix.m[13],matrix.m[14]};
  if(dot(origin-definitions.player.position,origin-definitions.player.position)>.000001f){error="physics and hierarchy player origins differ";return false;}
 }
 if(jointDefinitions){if(!actorDefinitions||!graphDefinitions){error="joint session requires actor and hierarchy packs";return false;}if(!world.initializeJoints(*jointDefinitions,*actorDefinitions,*graphDefinitions,error))return false;}
 if(actorDefinitions){if(!graphDefinitions){error="actor session requires authored hierarchy";return false;}if(!actorScene.initialize(*actorDefinitions,*graphDefinitions,hierarchy,world,error))return false;}
 activation=graphDefinitions?std::make_unique<SceneActivation>(*graphDefinitions,hierarchy,world):nullptr;
 ground=GroundState{};slope=GroundState{};slope.slopeCheck=true;groundPlayer={};movement={};jumping={};dashing={};posture={};groundOffset={};slideSafety=0;clearPendingInput();slideCanceled=false;clearEffectRequests();gameTime=0;pendingEvent.clear();previousPosition=position();
 return true;
}
bool NativeSession::observe(GroundState&state,bool isSlope,std::string&error){
 if(!world.groundOverlaps(isSlope,false,contacts,error))return false;
 // Engine callbacks dispatch enters/exits, then GroundCheckGroup.Update calls
 // UpdateState. Keep contact bookkeeping distinct from state evaluation.
 previousContacts.assign(state.contacts.begin(),state.contacts.end());
 for(auto id:previousContacts)if(std::find(contacts.begin(),contacts.end(),id)==contacts.end()){auto*c=collider(id);if(c)state.exitContact(*c,groundPlayer,*this);}
 for(auto id:contacts){auto*c=collider(id);if(c)state.enterContact(*c,false,groundPlayer,*this);}
 return true;
}
bool NativeSession::tick(float moveX,float moveY,float yaw,float deltaTime,std::string&error){
 if(!std::isfinite(moveX)||!std::isfinite(moveY)||!std::isfinite(yaw)||!std::isfinite(deltaTime)||std::abs(deltaTime-.008f)>1e-7f){error="native session requires finite input and 8 ms tick";return false;}
 if(actorDefinitions&&!actorScene.beforeStep(deltaTime,error))return false;
 if(activation){ActivationCounts counts;if(!activation->flush(counts,error))return false;}
 if(!observe(ground,false,error)||!observe(slope,true,error))return false;
 jumping.updateTimers(gameTime);
 GroundFrame frame;frame.deltaTime=deltaTime;frame.gameTime=gameTime;frame.position=position();frame.jumpHeld=jumpHeld;
 ground.updateState(frame,groundPlayer,*this);slope.updateState(frame,groundPlayer,*this);
 if(!pendingEvent.empty()){error=pendingEvent;return false;}
 const auto&p=definitions.player;MovementInput input;input.velocity=velocity();input.forward={std::sin(yaw),0,std::cos(yaw)};input.right={std::cos(yaw),0,-std::sin(yaw)};
 input.inputDirection=input.right*moveX+input.forward*moveY;float length=std::sqrt(dot(input.inputDirection,input.inputDirection));if(length>1)input.inputDirection=input.inputDirection*(1/length);
 input.gravityDirection=normalized(p.gravity);input.deltaTime=deltaTime;input.fixedDeltaTime=deltaTime;input.walkSpeed=p.walkSpeed;input.airAcceleration=p.airAcceleration;input.mass=p.mass;
 input.grounded=ground.onGround;input.onSlope=slope.onGround;
 if(ground.onGround){jumping.fallTime=0;if(!jumping.jumpCooldown)jumping.falling=false;}
 else if(jumping.fallTime<1)jumping.fallTime+=deltaTime*5;else jumping.falling=true;
 auto surface=bySurface.find(groundPlayer.surface);
 if(surface!=bySurface.end()){
  const auto&c=*surface->second;input.hasSurface=true;input.friction=c.surfaceFriction;input.surfaceSpeedMultiplier=c.surfaceSpeed;input.surfacePush=bool(c.surfaceFlags&PushSurface);input.surfacePushWorld={c.surfacePush[0],c.surfacePush[1],c.surfacePush[2]};
 }
 jumping.boostCharge=rechargeStamina(jumping.boostCharge,deltaTime,2,jumping.sliding,jumping.slowMode);
 if(jumpPending){
  jumpPending=false;
  // Wall/enemy jumps require their native contact systems; this is the
  // authored ordinary/coyote jump path, with the existing falling guard.
  if(!jumping.jumpCooldown&&!jumping.falling){
   JumpInput ji;ji.velocity=velocity();ji.up=p.up;ji.inputDirection=input.inputDirection;ji.deltaTime=deltaTime;ji.fixedDeltaTime=deltaTime;ji.walkSpeed=p.walkSpeed;ji.jumpPower=p.jumpPower;ji.gameTime=gameTime;
   ji.superChance=ground.superJumpChance;ji.bounceChance=ground.bounceChance;ji.extraChance=ground.extraJumpChance;ji.slamForce=groundPlayer.slamForce;ji.heavyFall=ground.heavyFall;
   if(surface!=bySurface.end()){const auto&c=*surface->second;ji.hasSurface=true;ji.canJump=c.surfaceFlags&CanJump;ji.silentJumpFail=c.surfaceFlags&SilentJumpFail;ji.surfaceJumpMultiplier=c.surfaceJump;}
   jumping.jumpTimestamp=jumpPressedAt;auto result=jump(ji,jumping);
   if(effects.size()+result.eventCount>256){error="native effect request queue not being consumed";return false;}
   effects.insert(effects.end(),result.events.begin(),result.events.begin()+result.eventCount);
   if(result.clearBounceAndHeavyFall){ground.bounceChance=0;ground.heavyFall=false;}
   world.applyMovement(result.physics);
  }
 }
 DashInput di;di.velocity=velocity();di.inputDirection=input.inputDirection;di.forward=input.forward;di.right=input.right;di.up=p.up;di.position=position();di.gravityDirection=input.gravityDirection;di.walkSpeed=p.walkSpeed;di.deltaTime=deltaTime;di.fixedDeltaTime=deltaTime;di.friction=input.friction;di.grounded=ground.onGround;di.heavyFall=ground.heavyFall;di.leftInput=moveX;
 if(surface!=bySurface.end()){const auto&c=*surface->second;di.hasSurface=true;di.canDash=c.surfaceFlags&CanDash;di.silentDashFail=c.surfaceFlags&SilentDashFail;di.canSlide=c.surfaceFlags&CanSlide;di.surfaceSpeed=c.surfaceSpeed;di.pushingSurface=c.surfaceFlags&PushSurface;di.surfacePush=input.surfacePushWorld;}
 auto recordDash=[&](const DashResult&result){
  if(dashEffects.size()+result.eventCount>256||effects.size()+result.slideEffects.eventCount>256){error="native effect request queue not being consumed";return false;}
  dashEffects.insert(dashEffects.end(),result.events.begin(),result.events.begin()+result.eventCount);effects.insert(effects.end(),result.slideEffects.events.begin(),result.slideEffects.events.begin()+result.slideEffects.eventCount);
  if(result.clearHeavyFall)ground.heavyFall=false;return true;
 };
 if(slidePending){
  slidePending=false;
  bool below=world.ray(world.groundProbePosition()+p.up,p.up*-2,EnvironmentLayers,false).hit;
  bool nearGround=ground.onGround||ground.lastGroundedTime<.06f||float(gameTime)-jumping.lastJumpTime<.06f||below;
  if(nearGround&&(!jumping.slowMode||posture.crouching)&&!jumping.sliding){
   SlideStartInput si;si.dash=di;si.crouching=posture.crouching;si.capsuleHeight=world.playerHeight();if(surface!=bySurface.end())si.silentSlideFail=surface->second->surfaceFlags&SilentSlideFail;
   jumping.slideTimestamp=slidePressedAt;auto result=startSlide(si,jumping,dashing);if(!recordDash(result.effects))return false;
   if(result.resize){groundOffset=result.groundOffset;if(!world.setPlayerCapsule(1.25f,result.positionDelta,groundOffset,error))return false;}
   if(result.effects.accepted)slideSafety=result.slideSafety;
  }
 }
 if(dashPending){dashPending=false;if(!jumping.slowMode){if(!recordDash(tryDash(di,jumping,dashing)))return false;}}
 CrouchInput ci;ci.sliding=jumping.sliding;ci.slideCanceled=slideCanceled;ci.up=p.up;ci.capsuleHeight=world.playerHeight();
 if(surface!=bySurface.end()){ci.hasSurface=true;ci.forceCrouch=surface->second->surfaceFlags&ForceCrouch;}
 if(!ci.sliding&&!ci.forceCrouch&&ci.capsuleHeight!=3.5f){
  Vec3 bottom=world.playerCapsuleBottom();ci.overheadRay=world.ray(bottom,p.up*3.5f,EnvironmentLayers,false).hit;RayHit obstruction;
  if(!ci.overheadRay){if(!world.sphereCast(bottom+p.up*.25f,.5f,p.up*2,EnvironmentLayers,false,obstruction,error))return false;ci.overheadSphere=obstruction.hit;}
  if(!ci.overheadRay&&!ci.overheadSphere)ci.floorRay=world.ray(position(),p.up*-2.25f,EnvironmentLayers,false).hit;
 }
 posture.slowMode=jumping.slowMode;auto pose=crouchPose(ci,posture);slideCanceled=false;
 if(pose.stopSlide){JumpResult result;stopSlideNumeric(jumping,result,ground.heavyFall);if(effects.size()+result.eventCount>256){error="native effect request queue not being consumed";return false;}effects.insert(effects.end(),result.events.begin(),result.events.begin()+result.eventCount);}
 if(pose.updateSlide){jumping.slideLength+=deltaTime;if(slideSafety>0)slideSafety-=deltaTime*5;}
 if(pose.setHeight){if(pose.setGroundOffset)groundOffset=pose.groundOffset;if(!world.setPlayerCapsule(pose.height,pose.positionDelta,groundOffset,error))return false;}
 jumping.slowMode=posture.slowMode;
 // Camera smoothing, portal travel and visual cleanup are separate backends.
 // Pose decisions retain their requests in the calculation module.
 if(jumping.sliding&&slideSafety<=0){
  Vec3 v=velocity();float horizontal=std::sqrt(v.x*v.x+v.z*v.z);float threshold=10;if(di.hasSurface&&di.surfaceSpeed<1)threshold*=di.surfaceSpeed;
  if(horizontal<threshold){slideSafety=std::max(-.1f,slideSafety-deltaTime);if(slideSafety<=-.1f){JumpResult result;stopSlideNumeric(jumping,result,ground.heavyFall);if(effects.size()+result.eventCount>256){error="native effect request queue not being consumed";return false;}effects.insert(effects.end(),result.events.begin(),result.events.begin()+result.eventCount);}}
  else slideSafety=0;
 }
 jumping.windState-=deltaTime;input.windState=jumping.windState;input.jumping=jumping.jumping;input.slowMode=jumping.slowMode;input.velocity=velocity();
 updatePreSlideSpeed(deltaTime,ground.heavyFall,groundPlayer.slamForce,velocity(),jumping,dashing);
 previousPosition=position();
 if(jumping.boost){di.velocity=velocity();auto result=dodge(di,jumping,dashing,movement);movement.slideEnding=jumping.slideEnding;if(!recordDash(result))return false;world.applyMovement(result.physics);}
 else{movement.slideEnding=jumping.slideEnding;world.applyMovement(move(input,movement));jumping.slideEnding=movement.slideEnding;}
 if(!jumping.boost||dashing.boostLeft<=0)jumping.preDashSpeed=velocity();
 if(!world.setLayerCollision(2,12,!(ground.heavyFall||jumping.sliding),error)||!world.setPlayerLayer(unsigned(movement.layer),error)||!world.step(deltaTime,error))return false;
 if(graphDefinitions){if(!hierarchy.setWorldPosition(playerNode,position(),error)||!hierarchy.setLayer(playerNode,unsigned(movement.layer),error)||!hierarchy.setWorldPosition(groundNode,world.groundProbePosition(),error))return false;}
 if(actorDefinitions&&!actorScene.afterStep(error))return false;
 gameTime+=deltaTime;return true;
}
}
