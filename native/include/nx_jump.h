#pragma once
#include "nx_movement.h"
#include <array>
#include <limits>
namespace nx::game {
enum class JumpEventKind {DeniedSound,JumpAnimation,QuakeSound,JumpSound,StopSlideEffects,DashJumpSound,StaminaFailSound,SSJIndicator};
struct JumpEvent {JumpEventKind kind;float a=0,b=0;};
struct JumpInput {
 Vec3 velocity{},up{0,1,0},inputDirection{};
 float deltaTime=.008f,fixedDeltaTime=.008f,walkSpeed=0,jumpPower=0;
 float superChance=0,bounceChance=0,extraChance=0,slamForce=0,surfaceJumpMultiplier=1;
 double gameTime=0;
 bool modNoJump=false,hasSurface=false,canJump=true,silentJumpFail=false,quakeJump=false;
 bool assistMajor=false,infiniteStamina=false,ssjIndicator=false,heavyFall=false;
};
struct JumpState {
 bool jumping=false,falling=false,jumpCooldown=false,sliding=false,slideEnding=false,slowMode=false,boost=false,enemyStepping=false;
 float fallTime=0,lastJumpTime=0,boostCharge=300,windState=0,slideLength=0,longestSlide=0,ssjMaxFrames=4;
 int framesSinceSlide=0;
 Vec3 dodgeDirection{},preDashSpeed{},velocityAfterSlide{};
 double jumpTimestamp=0,slideTimestamp=0;
 double notJumpingAt=std::numeric_limits<double>::infinity(),jumpReadyAt=std::numeric_limits<double>::infinity();
 void updateTimers(double now){if(now>=notJumpingAt){jumping=false;notJumpingAt=std::numeric_limits<double>::infinity();}if(now>=jumpReadyAt){jumpCooldown=false;jumpReadyAt=std::numeric_limits<double>::infinity();}}
};
struct JumpResult {
 bool accepted=false,clearBounceAndHeavyFall=false,restoreEnemyTriggerCollision=false;
 MovementResult physics;
 std::array<JumpEvent,12> events{};unsigned eventCount=0;
 void emit(JumpEventKind kind,float a=0,float b=0){events[eventCount++]={kind,a,b};}
};
// Ported Jump, numeric StopSlide work and jump-specific TrySSJ. Audio, camera,
// animations and rumble are explicit events for their corresponding systems.
JumpResult jump(const JumpInput&,JumpState&);
void stopSlideNumeric(JumpState&,JumpResult&,bool heavyFall);
}
