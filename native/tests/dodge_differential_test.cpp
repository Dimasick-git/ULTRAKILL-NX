#include "nx_dash.h"
#include "jump_reference_support.h"
#include "dodge_original.inc"
#include <random>
#include <cstdio>
#include <cstdlib>
using namespace nx::game;namespace ref=jump_reference;
int caseIndex=0;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL case %d line %d: %s\n",caseIndex,__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b){if(std::abs(a-b)>std::max(.00015f,std::max(std::abs(a),std::abs(b))*.000004f)){std::fprintf(stderr,"values %.9g %.9g\n",a,b);CHECK(false);}}
void near(nx::Vec3 a,nx::Vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
int main(){std::mt19937 rng(0xD0D6E);std::uniform_real_distribution<float> f(-1,1);
 for(caseIndex=0;caseIndex<150000;caseIndex++){
  unsigned flags=rng();DashInput i;JumpState s;DashState d;MovementState state;
  i.velocity={f(rng)*150,f(rng)*150,f(rng)*150};i.up=nx::normalized({f(rng),f(rng),f(rng)});i.gravityDirection=nx::normalized({f(rng),f(rng),f(rng)});i.right=nx::normalized({f(rng),f(rng),f(rng)});
  i.inputDirection={f(rng),0,f(rng)};i.pushForce={f(rng)*10,f(rng)*10,f(rng)*10};i.surfacePush={f(rng)*10,f(rng)*10,f(rng)*10};i.leftInput=f(rng)*2;
  i.walkSpeed=caseIndex%4?750:250;i.deltaTime=caseIndex%2?.008f:1.f/60;i.fixedDeltaTime=.008f;
  i.friction=caseIndex%3?float(caseIndex%5)*.25f:0;i.surfaceSpeed=float(caseIndex%5)*.5f;i.hurtInvincibility=flags&1?1:0;i.levelOver=flags&2;
  i.grounded=flags&4;i.onWall=flags&8;i.hasSurface=flags&16;i.canSlide=flags&32;i.modNoDashSlide=(flags&127)==0;i.pushingSurface=flags&128;i.hookBeingPulled=flags&256;
  s.sliding=flags&512;s.boost=true;s.slideEnding=flags&1024;s.framesSinceSlide=caseIndex%7;s.dodgeDirection=caseIndex%2?nx::Vec3{0,0,1}:nx::normalized({f(rng),f(rng),f(rng)});if(caseIndex%7==0)i.up={0,1,0};
  d.preSlideSpeed=float(caseIndex%10)*.5f;d.preSlideDelay=.5f;d.boostLeft=float(caseIndex%5)*4;d.dashStorage=float(caseIndex%4)*.008f;d.dashedFromGround=flags&2048;state.layer=15;state.exploded=true;
  ref::Rigidbody body;body.velocity=i.velocity;body.gravity=i.gravityDirection;ref::GroundCheckGroup gc;gc.onGround=i.grounded;ref::WallCheckGroup wall;wall.onWall=i.onWall;
  ref::CustomGroundProperties ground;ground.canSlide=i.canSlide;ground.speedMultiplier=i.surfaceSpeed;ground.push=i.pushingSurface;ground.pushForce=i.surfacePush;
  ref::InputActionState action;action.value.x=i.leftInput;ref::PlayerInput playerInput;playerInput.Move=&action;ref::input.source=&playerInput;ref::hook.beingPulled=i.hookBeingPulled;
  ref::NewMovement m{};m.rb=&body;m.gc=&gc;m.wcGroup=&wall;m.groundProperties=i.hasSurface?&ground:nullptr;m.transform.up=i.up;m.transform.right=i.right;
  m.hurtInvincibility=i.hurtInvincibility;m.levelOver=i.levelOver;m.friction=i.friction;m.modNoDashSlide=i.modNoDashSlide;m.walkSpeed=i.walkSpeed;m.pushForce=i.pushForce;m.inputDir=i.inputDirection;
  m.sliding=s.sliding;m.boost=s.boost;m.slideEnding=s.slideEnding;m.framesSinceSlide=s.framesSinceSlide;m.dodgeDirection=s.dodgeDirection;
  m.preSlideSpeed=d.preSlideSpeed;m.preSlideDelay=d.preSlideDelay;m.boostLeft=d.boostLeft;m.dashStorage=d.dashStorage;m.dashedFromGround=d.dashedFromGround;m.gameObject.layer=state.layer;m.exploded=state.exploded;
  ref::dt=i.deltaTime;reference::fixedDt=i.fixedDeltaTime;ref::effects.clear();ref::originalDodge(&m);auto r=dodge(i,s,d,state);
  near(r.physics.velocity,body.velocity);CHECK(r.physics.velocityWritten==body.written);near(d.preSlideSpeed,m.preSlideSpeed);near(d.preSlideDelay,m.preSlideDelay);near(d.boostLeft,m.boostLeft);near(d.dashStorage,m.dashStorage);
  CHECK(s.boost==m.boost&&d.dashedFromGround==m.dashedFromGround&&state.layer==m.gameObject.layer&&state.exploded==m.exploded);near(state.targetVelocity,m.targetVel);
  if(!m.stopSlideCalls)CHECK(s.slideEnding==m.slideEnding&&s.sliding==m.sliding);
  CHECK((r.slideEffects.eventCount>0)==(m.stopSlideCalls>0));if(r.inputDirectionWritten)near(r.inputDirection,m.inputDir);
  std::vector<int> expected;for(unsigned n=0;n<r.eventCount;n++){CHECK(r.events[n].kind==DashEventKind::SlideScrape);expected.push_back(9);}CHECK(expected==ref::effects);
 }
 std::puts("PASS: 150000 cases vs archived TEST16 Dodge: slide friction/speed/side steering, surface pushes, dash duration/angle/velocity, layer changes, hook and post-slide guards. StopSlide and effect internals excluded; relative pushes resolved outside this boundary.");
}
