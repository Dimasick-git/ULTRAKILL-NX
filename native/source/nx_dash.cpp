#include "nx_dash.h"
#include <cmath>
#include <algorithm>
namespace nx::game {
namespace {
Vec3 unit(Vec3 v){float m=std::sqrt(dot(v,v));return m>1e-5f?Vec3{v.x/m,v.y/m,v.z/m}:Vec3{};}
Vec3 plane(Vec3 v,Vec3 n){float sq=dot(n,n);if(sq<std::numeric_limits<float>::denorm_min())return v;float d=dot(v,n);return {v.x-n.x*d/sq,v.y-n.y*d/sq,v.z-n.z*d/sq};}
bool equal(Vec3 a,Vec3 b){Vec3 d=a-b;return dot(d,d)<9.99999944e-11f;}
float angle(Vec3 a,Vec3 b){float denominator=std::sqrt(dot(a,a)*dot(b,b));if(denominator<1e-15f)return 0;return std::acos(std::clamp(dot(a,b)/denominator,-1.f,1.f))*57.29578f;}
}
DashResult tryDash(const DashInput&i,JumpState&s,DashState&d){
 DashResult out;out.physics.velocity=i.velocity;d.dashedFromGround=i.grounded;
 if((i.hasSurface&&!i.canDash)||i.modNoDashSlide||s.boostCharge<100){if(!i.modNoDashSlide&&!(i.hasSurface&&i.silentDashFail))out.emit(DashEventKind::StaminaFail);return out;}
 out.accepted=true;if(s.sliding)stopSlideNumeric(s,out.slideEffects,i.heavyFall);
 d.boostLeft=100;d.dashStorage=1;s.boost=true;s.dodgeDirection=equal(i.inputDirection,{})?i.forward:i.inputDirection;
 out.emit(DashEventKind::Particle,i.position+s.dodgeDirection*10,s.dodgeDirection*-1);
 if(!i.assistMajor||!i.infiniteStamina)s.boostCharge-=100;
 d.cameraDirection=equal(s.dodgeDirection,i.forward)?0:(equal(s.dodgeDirection,i.forward*-1)?1:2);
 out.emit(DashEventKind::Sound);out.emit(DashEventKind::Rumble);
 if(i.heavyFall){d.fallSpeed=0;out.clearHeavyFall=true;out.emit(DashEventKind::StopFallEffects);}
 return out;
}
DashResult dodge(const DashInput&i,JumpState&s,DashState&d,MovementState&m){
 DashResult out;out.physics.velocity=i.velocity;
 if(s.sliding){
  if(i.hurtInvincibility<=0&&!i.levelOver&&d.boostLeft<=0){m.layer=2;m.exploded=false;}
  float multiplier=1;
  if(d.preSlideSpeed>1){if(d.preSlideSpeed>3)d.preSlideSpeed=3;multiplier=d.preSlideSpeed;if(i.grounded&&i.friction!=0)d.preSlideSpeed-=i.fixedDeltaTime*d.preSlideSpeed*i.friction;d.preSlideDelay=0;}
  if(i.modNoDashSlide||(i.hasSurface&&!i.canSlide)){stopSlideNumeric(s,out.slideEffects,i.heavyFall);return out;}
  if(i.hasSurface)multiplier*=i.surfaceSpeed;
  Vec3 opposite=i.gravityDirection*-1;float vertical=dot(opposite,i.velocity);
  Vec3 target=plane(s.dodgeDirection,i.gravityDirection)*i.walkSpeed*i.deltaTime*4*multiplier+opposite*vertical;
  if(i.hasSurface&&i.pushingSurface)target=target+i.surfacePush;
  if(i.grounded||i.onWall)out.emit(DashEventKind::SlideScrape);
  if(d.boostLeft>0){d.dashStorage=std::max(0.f,d.dashStorage-i.fixedDeltaTime);if(d.dashStorage<=0)d.boostLeft=0;}
  Vec3 lateral=unit(plane(i.right,i.gravityDirection))*i.leftInput;
  float sq=dot(lateral,lateral);if(sq>1)lateral=unit(lateral);
  lateral=lateral*5;
  out.inputDirectionWritten=true;out.inputDirection=lateral;
  if(!i.hookBeingPulled){out.physics.velocity=target+i.pushForce+lateral;out.physics.velocityWritten=true;}
  else stopSlideNumeric(s,out.slideEffects,i.heavyFall);
  return out;
 }
 if(s.framesSinceSlide<=3)return out;
 float vertical=s.slideEnding?dot(i.gravityDirection*-1,i.velocity):0;
 float directionAngle=angle(i.up,s.dodgeDirection);if(directionAngle<85||directionAngle>95){s.boost=false;return out;}
 if(i.grounded)d.dashedFromGround=true;
 m.targetVelocity=s.dodgeDirection*i.walkSpeed*i.deltaTime*2.75f+i.gravityDirection*-1*vertical;m.layer=15;
 if(s.slideEnding){s.slideEnding=false;if(!i.grounded||i.friction==0){s.boost=false;return out;}}
 if(d.boostLeft>0){out.physics.velocity=m.targetVelocity*3;out.physics.velocityWritten=true;d.boostLeft-=4;return out;}
 if(!i.grounded||i.friction!=0){out.physics.velocity=m.targetVelocity;out.physics.velocityWritten=true;}
 s.boost=false;return out;
}
SlideStartResult startSlide(const SlideStartInput&input,JumpState&s,DashState&d){
 const auto&i=input.dash;SlideStartResult out;out.effects.emit(DashEventKind::SlideCleanup);
 if(i.modNoDashSlide){stopSlideNumeric(s,out.effects.slideEffects,i.heavyFall);return out;}
 if(i.hookBeingPulled)return out;
 if(i.hasSurface&&!i.canSlide){if(!input.silentSlideFail)stopSlideNumeric(s,out.effects.slideEffects,i.heavyFall);return out;}
 if(!input.crouching){out.resize=true;out.positionDelta=i.up*-.5f*(input.capsuleHeight-1.25f);out.groundOffset=i.up*1.125f;}
 out.effects.accepted=true;out.slideSafety=1;s.sliding=true;s.boost=true;s.dodgeDirection=equal(i.inputDirection,{})?i.forward:i.inputDirection;
 out.effects.emit(DashEventKind::SlideParticle,i.position+out.positionDelta+s.dodgeDirection*10,s.dodgeDirection*-1);
 out.effects.emit(DashEventKind::SlideScrape);
 d.cameraDirection=equal(s.dodgeDirection,i.forward)?0:(equal(s.dodgeDirection,i.forward*-1)?1:2);
 out.effects.emit(DashEventKind::SlideRumble);return out;
}
void updatePreSlideSpeed(float dt,bool heavy,float slam,Vec3 velocity,JumpState&s,DashState&d){
 if(s.sliding)return;
 if(s.framesSinceSlide<std::numeric_limits<int>::max())s.framesSinceSlide++;
 if(heavy){d.preSlideDelay=.2f;d.preSlideSpeed=slam;return;}
 float speed=std::sqrt(dot(velocity,velocity))/24;
 if(!s.boost&&s.falling&&speed>d.preSlideSpeed){d.preSlideSpeed=speed;d.preSlideDelay=.2f;return;}
 d.preSlideDelay=std::max(0.f,d.preSlideDelay-dt);
 if(d.preSlideDelay<=0){d.preSlideDelay=.2f;d.preSlideSpeed=speed;}
}
}
