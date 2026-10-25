#include "nx_jump.h"
#include "jump_reference_support.h"
#include "jump_original.inc"
#include <random>
#include <cstdio>
#include <cstdlib>
using namespace nx::game;
namespace ref=jump_reference;
int caseIndex=0;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL case %d line %d: %s\n",caseIndex,__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b){if(std::abs(a-b)>std::max(.0001f,std::max(std::abs(a),std::abs(b))*.000002f)){std::fprintf(stderr,"mismatch %.9g %.9g\n",a,b);CHECK(false);}}
void near(nx::Vec3 a,nx::Vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
int main(){
 std::mt19937 random(0x162653);std::uniform_real_distribution<float> scalar(-200,200),positive(0,1000),unit(-1,1);
 for(caseIndex=0;caseIndex<120000;caseIndex++){
  JumpInput i;JumpState s;unsigned flags=random();
  i.velocity={scalar(random),scalar(random),scalar(random)};i.up=nx::normalized({unit(random),unit(random),unit(random)});i.inputDirection={unit(random),0,unit(random)};
  i.walkSpeed=positive(random);i.jumpPower=positive(random);i.gameTime=positive(random);i.deltaTime=(caseIndex%2?.008f:1.f/60);i.fixedDeltaTime=.008f;
  i.hasSurface=flags&1;i.canJump=flags&2;i.modNoJump=(flags&15)==0;i.silentJumpFail=flags&16;i.surfaceJumpMultiplier=positive(random)*.01f;
  i.assistMajor=flags&32;i.infiniteStamina=flags&64;i.quakeJump=flags&128;
  i.superChance=flags&256?.1f:0;i.bounceChance=flags&512?.1f:0;i.extraChance=flags&1024?.1f:0;i.slamForce=float(caseIndex%13)*.5f;
  s.sliding=flags&2048;s.slowMode=flags&4096;s.boost=flags&8192;s.enemyStepping=flags&16384;s.boostCharge=float(caseIndex%7)*50;
  s.jumpTimestamp=caseIndex%2?1:0;s.slideTimestamp=0;s.ssjMaxFrames=4;s.fallTime=3;s.windState=unit(random);
  s.dodgeDirection={unit(random),unit(random),unit(random)};
  ref::Rigidbody body;body.velocity=i.velocity;ref::GroundCheckGroup gc{i.superChance,i.bounceChance,i.extraChance,true};
  ref::CustomGroundProperties ground{i.canJump,i.silentJumpFail,i.surfaceJumpMultiplier};ref::AssistController assists{i.assistMajor,i.infiniteStamina};ref::AudioSource audio;ref::AudioClip clip;
  ref::GameObject quake{2},dash{3},fail{4};ref::NewMovement m{};
  m.rb=&body;m.gc=&gc;m.groundProperties=i.hasSurface?&ground:nullptr;m.asscon=&assists;m.aud=&audio;m.jumpSound=&clip;m.quakeJumpSound=&quake;m.dashJumpSound=&dash;m.staminaFailSound=&fail;m.transform.up=i.up;
  m.modNoJump=i.modNoJump;m.quakeJump=i.quakeJump;m.jumpPower=i.jumpPower;m.walkSpeed=i.walkSpeed;m.inputDir=i.inputDirection;m.slamForce=i.slamForce;
  m.sliding=s.sliding;m.slowMode=s.slowMode;m.boost=s.boost;m.enemyStepping=s.enemyStepping;m.boostCharge=s.boostCharge;m.windState=s.windState;m.fallTime=s.fallTime;
  m.jumpTimestamp=s.jumpTimestamp;m.slideTimestamp=s.slideTimestamp;m.ssjMaxFrames=s.ssjMaxFrames;m.dodgeDirection=s.dodgeDirection;
  ref::now=i.gameTime;ref::dt=i.deltaTime;ref::effects.clear();ref::originalJump(&m);
  auto r=jump(i,s);
  CHECK(r.accepted==m.jumping);CHECK(r.physics.velocityWritten==body.written);near(r.physics.velocity,body.velocity);near(r.physics.queuedForce,body.force);
  near(s.fallTime,m.fallTime);near(s.lastJumpTime,m.lastJump.time);near(s.boostCharge,m.boostCharge);near(s.windState,m.windState);
  CHECK(s.jumping==m.jumping&&s.falling==m.falling&&s.boost==m.boost&&s.enemyStepping==m.enemyStepping&&s.jumpCooldown==m.jumpCooldown);
  if(r.accepted){CHECK(s.notJumpingAt==m.notJumpingAt&&s.jumpReadyAt==m.jumpReadyAt);CHECK(m.trySSJCalls==1);near(m.ssjGain,.5f);near(m.ssjDirection,reference::Vector3_get_normalized(&m.dodgeDirection));CHECK(gc.bounceChance==0&&!gc.heavyFall);}
  else CHECK(m.trySSJCalls==0);
  CHECK(m.stopSlideCalls==(r.accepted&&(flags&2048)?1:0));
  std::vector<int> expected;for(unsigned n=0;n<r.eventCount;n++)switch(r.events[n].kind){
   case JumpEventKind::DeniedSound:case JumpEventKind::JumpSound:expected.push_back(1);near(audio.volume,r.events[n].a);near(audio.pitch,r.events[n].b);break;
   case JumpEventKind::QuakeSound:expected.push_back(2);break;case JumpEventKind::DashJumpSound:expected.push_back(3);break;
   case JumpEventKind::StaminaFailSound:expected.push_back(4);break;case JumpEventKind::JumpAnimation:expected.push_back(5);break;default:break;
  }
  CHECK(expected==ref::effects);
 }
 std::puts("PASS: 120000 cases vs archived TEST16 Jump body: force, velocity, stamina, flags, invoke deadlines and audio/animation requests. StopSlide/TrySSJ internals excluded.");
}
