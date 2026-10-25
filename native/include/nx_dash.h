#pragma once
#include "nx_jump.h"
namespace nx::game {
enum class DashEventKind {StaminaFail,Particle,Sound,Rumble,StopFallEffects,SlideScrape,SlideCleanup,SlideParticle,SlideRumble};
struct DashEvent {DashEventKind kind;Vec3 position{},direction{};};
struct DashInput {
 Vec3 velocity{},inputDirection{},forward{0,0,1},right{1,0,0},up{0,1,0},position{},gravityDirection{0,-1,0},pushForce{},surfacePush{};
 float walkSpeed=750,deltaTime=.008f,fixedDeltaTime=.008f,friction=1,surfaceSpeed=1,leftInput=0,hurtInvincibility=0;
 bool grounded=false,onWall=false,heavyFall=false,levelOver=false,modNoDashSlide=false,hasSurface=false,canDash=true,silentDashFail=false,canSlide=true,pushingSurface=false;
 bool assistMajor=false,infiniteStamina=false,hookBeingPulled=false;
};
struct DashState {float boostLeft=0,dashStorage=0,fallSpeed=0,preSlideSpeed=0,preSlideDelay=0;bool dashedFromGround=false;int cameraDirection=0;};
struct DashResult {
 MovementResult physics;JumpResult slideEffects;
 bool accepted=false,clearHeavyFall=false;
 bool inputDirectionWritten=false;Vec3 inputDirection{};
 std::array<DashEvent,6> events{};unsigned eventCount=0;
 void emit(DashEventKind kind,Vec3 position={},Vec3 direction={}){events[eventCount++]={kind,position,direction};}
};
// TryDash and Dodge calculations from TEST16, with engine effects represented
// as requests. Capsule resizing/slide input lifecycle are separate systems.
DashResult tryDash(const DashInput&,JumpState&,DashState&);
DashResult dodge(const DashInput&,JumpState&,DashState&,MovementState&);
struct SlideStartInput {DashInput dash;bool crouching=false,silentSlideFail=false;float capsuleHeight=3.5f;};
struct SlideStartResult {
 DashResult effects;bool resize=false;Vec3 positionDelta{},groundOffset{};float slideSafety=0;
};
SlideStartResult startSlide(const SlideStartInput&,JumpState&,DashState&);
// FixedUpdate's pre-slide speed sampling. A heavy fall also needs the separate
// sphere-cast/snap adapter; this function only updates the authored counters.
void updatePreSlideSpeed(float fixedDeltaTime,bool heavyFall,float slamForce,Vec3 velocity,JumpState&,DashState&);
}
