#include "nx_jump.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b,float tolerance=.001f){CHECK(std::abs(a-b)<=tolerance);}
bool event(const JumpResult&r,JumpEventKind kind){for(unsigned n=0;n<r.eventCount;n++)if(r.events[n].kind==kind)return true;return false;}
JumpInput input(){JumpInput i;i.walkSpeed=750;i.jumpPower=90;i.velocity={3,-15,4};i.gameTime=2;return i;}
int main(){
 auto i=input();JumpState s;auto r=jump(i,s);CHECK(r.accepted&&s.jumping&&s.falling&&s.jumpCooldown&&!s.boost&&r.clearBounceAndHeavyFall);near(r.physics.velocity.y,0);near(r.physics.velocity.x,3);near(r.physics.queuedForce.y,351000);CHECK(event(r,JumpEventKind::JumpAnimation)&&event(r,JumpEventKind::JumpSound));
 s.updateTimers(2.199);CHECK(s.jumpCooldown&&s.jumping);s.updateTimers(2.201);CHECK(!s.jumpCooldown&&s.jumping);s.updateTimers(2.25);CHECK(!s.jumping);
 i=input();i.modNoJump=true;s={};r=jump(i,s);CHECK(!r.accepted&&!s.jumping&&event(r,JumpEventKind::DeniedSound));near(s.lastJumpTime,2);
 i.modNoJump=false;i.hasSurface=true;i.canJump=false;i.silentJumpFail=true;r=jump(i,s);CHECK(!r.accepted&&r.eventCount==0);i.modNoJump=true;r=jump(i,s);CHECK(event(r,JumpEventKind::DeniedSound));
 i=input();i.hasSurface=true;i.surfaceJumpMultiplier=2;s={};r=jump(i,s);near(r.physics.queuedForce.y,702000);
 i=input();s={};s.slowMode=true;r=jump(i,s);near(r.physics.queuedForce.y,168750);
 i=input();s={};s.sliding=true;s.dodgeDirection={0,0,1};s.preDashSpeed={0,0,35};s.slideLength=20;s.longestSlide=10;r=jump(i,s);near(r.physics.queuedForce.y,270000);CHECK(!s.sliding&&s.slideEnding&&r.restoreEnemyTriggerCollision);near(s.longestSlide,20);near(s.velocityAfterSlide.z,35);CHECK(event(r,JumpEventKind::StopSlideEffects));
 s={};s.sliding=true;s.slowMode=true;i.heavyFall=true;r=jump(i,s);near(r.physics.queuedForce.y,135000);CHECK(!r.restoreEnemyTriggerCollision);
 i=input();s={};s.boost=true;s.jumpTimestamp=3;s.slideTimestamp=2;r=jump(i,s);near(s.boostCharge,200);near(r.physics.queuedForce.y,202500);CHECK(event(r,JumpEventKind::DashJumpSound));
 s={};s.boost=true;s.jumpTimestamp=3;s.slideTimestamp=2;i.assistMajor=true;i.infiniteStamina=true;r=jump(i,s);near(s.boostCharge,300);
 s={};s.boost=true;s.enemyStepping=true;s.jumpTimestamp=3;s.slideTimestamp=2;r=jump(i,s);near(s.windState,.5f);near(s.boostCharge,300);
 s={};s.boost=true;s.boostCharge=99;s.jumpTimestamp=3;s.slideTimestamp=2;i.inputDirection={1,0,0};r=jump(i,s);CHECK(event(r,JumpEventKind::StaminaFailSound));near(r.physics.velocity.x,16.5f);near(r.physics.velocity.z,0);
 for(float slam:{0.f,1.f,4.f,5.499f,5.5f,10.f}){i=input();i.superChance=.1f;i.slamForce=slam;s={};r=jump(i,s);near(r.physics.queuedForce.y,135000.f*(slam<5.5f?2.f+slam:12.5f),.25f);}
 for(int frame=0;frame<=4;frame++){
  i=input();i.ssjIndicator=true;s={};s.slideTimestamp=0;s.jumpTimestamp=(frame+.1)*double(.008f);s.dodgeDirection={0,0,1};s.velocityAfterSlide={0,0,24};r=jump(i,s);
  if(frame>0&&frame<4){CHECK(event(r,JumpEventKind::SSJIndicator));near(r.physics.velocity.z,24+24.75f/std::pow(2.f,float(frame-1)));}else CHECK(!event(r,JumpEventKind::SSJIndicator));
 }
 i=input();i.ssjIndicator=true;s={};s.jumpTimestamp=.01;s.dodgeDirection={0,0,1};s.velocityAfterSlide={0,0,500};r=jump(i,s);near(r.physics.velocity.z,100);
 std::puts("PASS: normal/surface/slow/slide/dash/enemy/super jump numerics, stamina failures/assists, cooldowns, SSJ timing/cap and effect requests; run check_jump.py separately for original-method comparison");
}
