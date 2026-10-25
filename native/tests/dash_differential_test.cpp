#include "nx_dash.h"
#include "jump_reference_support.h"
#include "dash_original.inc"
#include <random>
#include <cstdio>
#include <cstdlib>
using namespace nx::game;namespace ref=jump_reference;
int caseIndex=0;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL case %d line %d: %s\n",caseIndex,__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b){CHECK(std::abs(a-b)<=std::max(.0001f,std::max(std::abs(a),std::abs(b))*.000002f));}
void near(nx::Vec3 a,nx::Vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
int main(){std::mt19937 rng(0xDA516);std::uniform_real_distribution<float> f(-1,1);
 for(caseIndex=0;caseIndex<100000;caseIndex++){
  unsigned bits=rng();DashInput i;JumpState s;DashState d;
  i.hasSurface=bits&1;i.canDash=bits&2;i.silentDashFail=bits&4;i.modNoDashSlide=(bits&31)==0;i.grounded=bits&32;i.heavyFall=bits&64;i.assistMajor=bits&128;i.infiniteStamina=bits&256;
  i.forward=nx::normalized({f(rng),0,f(rng)});i.position={f(rng)*100,f(rng)*100,f(rng)*100};
  i.inputDirection=caseIndex%5?nx::Vec3{f(rng),0,f(rng)}:nx::Vec3{};if(caseIndex%11==0)i.inputDirection=i.forward*-1;if(caseIndex%13==0)i.inputDirection=i.forward;
  if(caseIndex%17==0)i.inputDirection={1e-6f,0,0};
  s.boostCharge=float(caseIndex%7)*50;s.boost=bits&512;s.sliding=bits&1024;s.dodgeDirection={3,0,1};d.fallSpeed=54;d.boostLeft=15;d.dashStorage=.25f;
  ref::GroundCheckGroup gc;gc.heavyFall=i.heavyFall;gc.onGround=i.grounded;ref::CustomGroundProperties ground;ground.canDash=i.canDash;ground.silentDashFail=i.silentDashFail;
  ref::AssistController assists{i.assistMajor,i.infiniteStamina};ref::AudioSource audio;ref::AudioClip clip;ref::GameObject particle{6},fail{4},fall{8};ref::CameraController camera;
  ref::NewMovement m{};m.gc=&gc;m.groundProperties=i.hasSurface?&ground:nullptr;m.asscon=&assists;m.aud=&audio;m.dodgeSound=&clip;m.staminaFailSound=&fail;m.dodgeParticle=&particle;m.currentFallParticle=&fall;m.cc=&camera;
  m.transform.forward=i.forward;m.transform.position=i.position;m.inputDir=i.inputDirection;m.modNoDashSlide=i.modNoDashSlide;m.boostCharge=s.boostCharge;m.boost=s.boost;m.sliding=s.sliding;m.dodgeDirection=s.dodgeDirection;m.fallSpeed=d.fallSpeed;m.boostLeft=d.boostLeft;m.dashStorage=d.dashStorage;
  ref::effects.clear();ref::originalTryDash(&m);auto r=tryDash(i,s,d);
  near(s.boostCharge,m.boostCharge);near(d.boostLeft,m.boostLeft);near(d.dashStorage,m.dashStorage);near(d.fallSpeed,m.fallSpeed);near(s.dodgeDirection,m.dodgeDirection);
  CHECK(s.boost==m.boost&&d.dashedFromGround==m.dashedFromGround&&d.cameraDirection==camera.dodgeDirection);CHECK(r.clearHeavyFall==(i.heavyFall&&!gc.heavyFall));
  CHECK(m.stopSlideCalls==(r.accepted&&(bits&1024)?1:0));std::vector<int> expected;
  for(unsigned n=0;n<r.eventCount;n++)switch(r.events[n].kind){case DashEventKind::StaminaFail:expected.push_back(4);break;case DashEventKind::Particle:expected.push_back(6);near(r.events[n].position,ref::particlePosition);near(r.events[n].direction,ref::particleDirection);break;case DashEventKind::Sound:expected.push_back(1);CHECK(audio.clip==&clip);near(audio.volume,1);near(audio.pitch,1);break;case DashEventKind::Rumble:expected.push_back(7);break;case DashEventKind::StopFallEffects:expected.push_back(8);break;default:CHECK(false);}
  CHECK(expected==ref::effects);
 }
 std::puts("PASS: 100000 cases vs archived TEST16 TryDash: surface/mod/stamina gates, assists, direction equality, stamina, heavy fall, camera direction and effect requests. StopSlide internals excluded.");
}
