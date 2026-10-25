#include "nx_movement.h"
#include <algorithm>
#include <limits>

namespace nx::game {
namespace {
float magnitude(Vec3 v){return std::sqrt(dot(v,v));}
Vec3 unit(Vec3 v){float length=magnitude(v);return length>1e-5f?Vec3{v.x/length,v.y/length,v.z/length}:Vec3{};}
Vec3 project(Vec3 v,Vec3 normal){float sq=dot(normal,normal);if(sq<std::numeric_limits<float>::denorm_min())return {};float d=dot(v,normal);return {normal.x*d/sq,normal.y*d/sq,normal.z*d/sq};}
Vec3 plane(Vec3 v,Vec3 normal){return v-project(v,normal);}
float lerp(float a,float b,float t){return a+(b-a)*std::clamp(t,0.f,1.f);}
Vec3 lerp(Vec3 a,Vec3 b,float t){return a+(b-a)*std::clamp(t,0.f,1.f);}
Vec3 clampLength(Vec3 v,float limit){float sq=dot(v,v);if(sq>limit*limit){float length=std::sqrt(sq);return {v.x/length*limit,v.y/length*limit,v.z/length*limit};}return v;}
}

MovementResult move(const MovementInput&i,MovementState&s){
 MovementResult out;out.velocity=i.velocity;
 s.slideEnding=false;
 if(i.hurtInvincibility<=0 && !i.levelOver){s.layer=2;s.exploded=false;}
 if(i.grounded && !i.jumping){s.wallJumps=0;s.rocketJumps=0;s.hammerJumps=0;s.rocketRides=0;}
 if(i.grounded && i.friction>0 && !i.jumping){
  float alongGravity=dot(unit(i.gravityDirection),i.velocity);
  if(i.onSlope && i.inputDirection.x==0 && i.inputDirection.z==0){alongGravity=0;out.useGravity=false;}
  float factor=i.slowMode?1.25f:2.75f;
  if(i.hasSurface)factor*=i.surfaceSpeedMultiplier;
  s.targetVelocity=i.inputDirection*(i.walkSpeed*i.deltaTime*factor)+i.gravityDirection*alongGravity;
  Vec3 push=i.pushForce;
  if(i.hasSurface && i.surfacePush)push=push+i.surfacePushWorld;
  out.velocity=lerp(i.velocity,s.targetVelocity+push,.25f*i.friction);out.velocityWritten=true;
  return out;
 }
 Vec3 desired=i.inputDirection*((i.slowMode?1.25f:2.75f)*i.deltaTime*i.walkSpeed);
 s.targetVelocity=desired+i.gravityDirection*dot(i.gravityDirection,i.velocity);
 Vec3 horizontal=plane(i.velocity,i.gravityDirection);
 s.airDirection={};
 float wind=i.windState>0?1.f:(i.windState>-.25f?std::clamp(i.windState/-.25f,0.f,1.f):0.f);
 float windSteering=lerp(1,2,wind);
 if(magnitude(i.inputDirection)>0){
  float acceleration=i.airAcceleration*lerp(1,4,wind)*i.fixedDeltaTime*(1/i.mass);
  Vec3 side=project(i.inputDirection,i.right),front=project(i.inputDirection,i.forward);
  float sideSpeed=dot(horizontal,unit(side)),sideAcceleration=acceleration;
  if(sideSpeed+sideAcceleration>16.5f)sideAcceleration=std::max(16.5f-sideSpeed,0.f);
  float frontSpeed=dot(horizontal,unit(front)),frontAcceleration=acceleration;
  if(frontSpeed+frontAcceleration>16.5f)frontAcceleration=std::max(16.5f-frontSpeed,0.f);
  Vec3 delta=side*sideAcceleration+front*frontAcceleration;
  Vec3 direction=magnitude(horizontal)>.001f?unit(horizontal):i.inputDirection;
  Vec3 perpendicular=plane(delta,direction),parallel=project(delta,direction);
  bool forwardAcceleration=dot(parallel,direction)>0,overspeed=magnitude(horizontal)>16.5f;
  s.airDirection=s.airDirection+perpendicular+parallel*((forwardAcceleration & overspeed)?lerp(.1f,.4f,1-wind):1.f);
  float steering=1-magnitude(s.airDirection)/(acceleration+.0001f);
  steering=std::pow(steering,3.f);
  float steeringLimit=i.airAcceleration*(1/i.mass)*i.fixedDeltaTime*windSteering*steering;
  Vec3 correction=i.inputDirection*magnitude(horizontal)-horizontal;
  Vec3 correctionParallel=project(correction,direction);
  correction=plane(correction,direction);
  correction=correction+correctionParallel*(dot(correctionParallel,horizontal)<0?lerp(.4f,1,1-wind):1.f);
  s.airDirection=s.airDirection+clampLength(correction,steeringLimit);
 }
 if(i.fakeFallRequests>0 && s.targetVelocity.x!=0 && s.airDirection.x==0 && s.targetVelocity.z!=0 && s.airDirection.z==0){
  Vec3 desiredHorizontal{s.targetVelocity.x,0,s.targetVelocity.z},currentHorizontal{i.velocity.x,0,i.velocity.z};
  Vec3 currentUnit=unit(currentHorizontal);
  out.queuedForce=(unit(desiredHorizontal)*i.airAcceleration)*3.f+(Vec3{-currentUnit.x,-currentUnit.y,-currentUnit.z}*i.airAcceleration)*3.f;
 }else out.queuedVelocityChange=s.airDirection;
 if(i.fakeFallRequests>0 && magnitude(i.inputDirection)<.01f){
  out.velocity=lerp(i.velocity,Vec3{},.01f-magnitude(i.inputDirection));out.velocityWritten=true;
 }
 return out;
}

float rechargeStamina(float charge,float dt,int difficulty,bool sliding,bool slowMode){
 if(charge==300 || sliding || slowMode)return charge;
 float multiplier=difficulty==1?1.5f:(difficulty==0?2.f:1.f),amount=70.f*dt*multiplier;
 if(std::abs(300.f-charge)<=amount)return 300.f;
 return charge+(300.f-charge>=0?1.f:-1.f)*amount;
}
}
