#include "nx_dash.h"
#include <cstdio>
#include <cstdlib>
using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b){CHECK(std::abs(a-b)<.001f);}
int main(){
 DashInput i;JumpState s;DashState d;MovementState m;s.framesSinceSlide=100;i.grounded=true;
 auto r=tryDash(i,s,d);CHECK(r.accepted&&s.boost&&d.dashedFromGround);near(s.boostCharge,200);near(s.dodgeDirection.z,1);
 for(unsigned tick=0;tick<25;tick++){r=dodge(i,s,d,m);CHECK(r.physics.velocityWritten&&s.boost&&m.layer==15);near(r.physics.velocity.z,49.5f);i.velocity=r.physics.velocity;}
 near(d.boostLeft,0);r=dodge(i,s,d,m);CHECK(!s.boost&&r.physics.velocityWritten);near(r.physics.velocity.z,16.5f);
 s.boost=true;s.dodgeDirection={0,1,0};r=dodge(i,s,d,m);CHECK(!s.boost&&!r.physics.velocityWritten);
 s.boost=true;s.dodgeDirection={0,0,1};s.framesSinceSlide=3;d.boostLeft=100;r=dodge(i,s,d,m);CHECK(s.boost&&!r.physics.velocityWritten);near(d.boostLeft,100);
 s.framesSinceSlide=4;s.slideEnding=true;i.grounded=false;r=dodge(i,s,d,m);CHECK(!s.boost&&!s.slideEnding&&!r.physics.velocityWritten);
 s={};d={};s.sliding=true;s.boost=true;s.dodgeDirection={0,0,1};i={};i.velocity={0,-5,0};i.grounded=true;d.preSlideSpeed=4;i.leftInput=1;
 r=dodge(i,s,d,m);near(r.physics.velocity.z,72);near(r.physics.velocity.x,5);near(r.physics.velocity.y,-5);near(d.preSlideSpeed,2.976f);CHECK(r.eventCount==1&&r.events[0].kind==DashEventKind::SlideScrape);
 i.hookBeingPulled=true;r=dodge(i,s,d,m);CHECK(!s.sliding&&s.slideEnding&&!r.physics.velocityWritten);
 s.sliding=true;i.hookBeingPulled=false;i.hasSurface=true;i.canSlide=false;r=dodge(i,s,d,m);CHECK(!s.sliding&&!r.physics.velocityWritten);
 s={};s.sliding=true;s.boostCharge=50;i={};r=tryDash(i,s,d);CHECK(!r.accepted&&s.sliding&&r.eventCount==1);i.modNoDashSlide=true;r=tryDash(i,s,d);CHECK(!r.accepted&&r.eventCount==0);
 SlideStartInput si;s={};d={};auto slide=startSlide(si,s,d);CHECK(slide.effects.accepted&&slide.resize&&s.sliding&&s.boost);near(slide.positionDelta.y,-1.125f);near(slide.groundOffset.y,1.125f);near(slide.slideSafety,1);
 si.crouching=true;s={};slide=startSlide(si,s,d);CHECK(slide.effects.accepted&&!slide.resize);si.dash.hasSurface=true;si.dash.canSlide=false;si.silentSlideFail=true;s={};slide=startSlide(si,s,d);CHECK(!slide.effects.accepted&&slide.effects.slideEffects.eventCount==0);si.silentSlideFail=false;slide=startSlide(si,s,d);CHECK(slide.effects.slideEffects.eventCount==1);
 s={};d={};s.falling=true;updatePreSlideSpeed(.008f,false,0,{0,-72,0},s,d);near(d.preSlideSpeed,3);near(d.preSlideDelay,.2f);CHECK(s.framesSinceSlide==1);
 s.falling=false;for(unsigned tick=0;tick<26;tick++)updatePreSlideSpeed(.008f,false,0,{},s,d);near(d.preSlideSpeed,0);s.sliding=true;int frames=s.framesSinceSlide;updatePreSlideSpeed(.008f,true,7,{},s,d);CHECK(frames==s.framesSinceSlide);near(d.preSlideSpeed,0);s.sliding=false;updatePreSlideSpeed(.008f,true,7,{},s,d);near(d.preSlideSpeed,7);
 std::puts("PASS: dash duration/speed/angle/post-slide guards; slide start/friction/steering, pre-slide speed and effect requests. Run check_dash.py separately for original-method comparison; full portal/wall/effect lifecycle remains incomplete.");
}
