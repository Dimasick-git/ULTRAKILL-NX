#include "nx_jump.h"
#include "jump_reference_support.h"
#include "stopslide_original.inc"
#include <random>
#include <cstdio>
#include <cstdlib>
using namespace nx::game;namespace ref=jump_reference;
int caseIndex=0;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL case %d line %d: %s\n",caseIndex,__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b){CHECK(std::abs(a-b)<=std::max(.0001f,std::max(std::abs(a),std::abs(b))*.000002f));}
int main(){std::mt19937 rng(0x570516);std::uniform_real_distribution<float> f(-100,100);
 for(caseIndex=0;caseIndex<60000;caseIndex++){
  unsigned flags=rng();JumpState s;s.sliding=flags&1;s.slideEnding=flags&2;s.slideLength=f(rng);s.longestSlide=f(rng);s.framesSinceSlide=20;s.preDashSpeed={f(rng),f(rng),f(rng)};s.dodgeDirection={f(rng),f(rng),f(rng)};if(caseIndex%9==0)s.dodgeDirection={};
  ref::GroundCheckGroup gc;gc.heavyFall=flags&4;ref::CameraController camera;ref::GameObject particle{8},scrape{10},sound{11};ref::NewMovement m{};
  m.gc=&gc;m.cc=&camera;m.currentSlideParticle=flags&8?&particle:nullptr;m.slideScrape=flags&16?&scrape:nullptr;m.slideStopSound=&sound;m.sliding=s.sliding;m.slideEnding=s.slideEnding;m.slideLength=s.slideLength;m.longestSlide=s.longestSlide;m.framesSinceSlide=s.framesSinceSlide;m.preDashSpeed=s.preDashSpeed;m.dodgeDirection=s.dodgeDirection;
  ref::effects.clear();ref::originalStopSlide(&m);JumpResult result;stopSlideNumeric(s,result,gc.heavyFall);
  CHECK(s.sliding==m.sliding&&s.slideEnding==m.slideEnding&&s.framesSinceSlide==m.framesSinceSlide);near(s.slideLength,m.slideLength);near(s.longestSlide,m.longestSlide);near(s.velocityAfterSlide.x,m.velocityAfterSlide.x);near(s.velocityAfterSlide.y,m.velocityAfterSlide.y);near(s.velocityAfterSlide.z,m.velocityAfterSlide.z);
  CHECK(result.restoreEnemyTriggerCollision==!gc.heavyFall);CHECK(result.eventCount==1&&result.events[0].kind==JumpEventKind::StopSlideEffects);
  std::vector<int> expected;if(flags&8)expected.push_back(8);if(flags&16)expected.push_back(10);expected.push_back(11);expected.push_back(12);if(!gc.heavyFall)expected.push_back(13);expected.push_back(14);CHECK(expected==ref::effects);
 }
 std::puts("PASS: 60000 cases vs archived TEST16 StopSlide: flags, slide records, velocityAfterSlide, collision restoration request and original effect boundary calls; effect implementations excluded.");
}
