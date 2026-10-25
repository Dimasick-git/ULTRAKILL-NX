#include "nx_jump.h"
#include <cmath>
#include <algorithm>
namespace nx::game {
namespace {
float length(Vec3 v){return std::sqrt(dot(v,v));}
Vec3 unit(Vec3 v){float n=length(v);return n>1e-5f?Vec3{v.x/n,v.y/n,v.z/n}:Vec3{};}
Vec3 plane(Vec3 v,Vec3 normal){float sq=dot(normal,normal);if(sq<std::numeric_limits<float>::denorm_min())return v;float d=dot(v,normal);return {v.x-normal.x*d/sq,v.y-normal.y*d/sq,v.z-normal.z*d/sq};}
void superSlideJump(const JumpInput&i,JumpState&s,JumpResult&r){
 double elapsed=s.jumpTimestamp-s.slideTimestamp;if(!(elapsed>0))return;
 int frame=int(elapsed/double(.008f));if(frame==0||float(frame)>=s.ssjMaxFrames)return;
 float loss=1.f/std::pow(2.f,float(frame-1));float base=.5f*i.walkSpeed*2.75f*3.f*i.fixedDeltaTime;float speed=loss*base;
 float y=r.physics.velocity.y;r.physics.velocity=s.velocityAfterSlide+unit(s.dodgeDirection)*speed;r.physics.velocity.y=y;
 r.physics.velocity=unit(r.physics.velocity)*std::min(length(r.physics.velocity),100.f);
 if(i.ssjIndicator)r.emit(JumpEventKind::SSJIndicator,float(frame),speed);
}
}
void stopSlideNumeric(JumpState&s,JumpResult&r,bool heavyFall){
 s.sliding=false;s.slideEnding=true;if(s.slideLength>s.longestSlide)s.longestSlide=s.slideLength;s.slideLength=0;s.framesSinceSlide=0;
 s.velocityAfterSlide=unit(s.dodgeDirection)*std::max(24.f,length(s.preDashSpeed));r.restoreEnemyTriggerCollision=!heavyFall;r.emit(JumpEventKind::StopSlideEffects);
}
JumpResult jump(const JumpInput&i,JumpState&s){
 JumpResult out;out.physics.velocity=i.velocity;s.fallTime=0;s.lastJumpTime=float(i.gameTime);float multiplier=1500.f;
 if(i.modNoJump||i.hasSurface){
  if(i.modNoJump||!i.canJump){if(i.modNoJump||!i.silentJumpFail)out.emit(JumpEventKind::DeniedSound,.75f,.25f);return out;}
  multiplier*=i.surfaceJumpMultiplier;
 }
 out.accepted=true;s.jumping=true;s.notJumpingAt=i.gameTime+double(.25f);s.falling=true;out.emit(JumpEventKind::JumpAnimation);
 if(i.quakeJump)out.emit(JumpEventKind::QuakeSound);
 bool boostedSound=i.superChance>0||i.bounceChance>0;out.emit(JumpEventKind::JumpSound,boostedSound?.85f:.75f,boostedSound?2.f:1.f);
 out.physics.velocity=plane(i.velocity,i.up);out.physics.velocityWritten=true;
 // Preserve multiply order used by the original force expressions.
 Vec3 force=i.up*i.jumpPower*multiplier;
 if(s.sliding){out.physics.queuedForce=force*(s.slowMode?1.f:2.f);stopSlideNumeric(s,out,i.heavyFall);}
 else if(s.boost && s.jumpTimestamp-s.slideTimestamp>double(.008f*s.ssjMaxFrames)){
  if(s.enemyStepping){out.emit(JumpEventKind::DashJumpSound);s.windState=.5f;}
  else if(s.boostCharge>=100){if(!i.assistMajor||!i.infiniteStamina)s.boostCharge-=100;out.emit(JumpEventKind::DashJumpSound);}
  else{out.physics.velocity={i.inputDirection.x*i.walkSpeed*i.deltaTime*2.75f,0,i.inputDirection.z*i.walkSpeed*i.deltaTime*2.75f};out.emit(JumpEventKind::StaminaFailSound);}
  out.physics.queuedForce=force*(s.slowMode?.75f:1.5f);
 }else if(s.slowMode)out.physics.queuedForce=force*1.25f;
 else if(i.superChance>0||i.bounceChance>0||i.extraChance>0)out.physics.queuedForce=force*(i.slamForce<5.5f?3.f+(i.slamForce-1.f):12.5f);
 else out.physics.queuedForce=force*2.6f;
 superSlideJump(i,s,out);s.jumpCooldown=true;s.jumpReadyAt=i.gameTime+double(.2f);s.boost=false;s.enemyStepping=false;out.clearBounceAndHeavyFall=true;
 return out;
}
}
