#include "nx_ground.h"
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
struct Bridge:GroundBridge {
 std::unordered_map<int64_t,ColliderRecord> records;std::unordered_set<int64_t> attached;
 unsigned attaches=0,detaches=0,shockwaves=0,bounces=0;float shockwaveMultiplier=0;
 const ColliderRecord*collider(int64_t id)const override{auto it=records.find(id);return it==records.end()?nullptr:&it->second;}
 bool tracked(int64_t id)const override{return attached.count(id);}
 void attach(int64_t id)override{attached.insert(id);attaches++;}
 void detach(int64_t id)override{attached.erase(id);detaches++;}
 void shockwave(float multiplier)override{shockwaves++;shockwaveMultiplier=multiplier;}
 void bounce(GroundState&s,nx::Vec3,GroundPlayerState&)override{bounces++;s.heavyFall=false;}
};
ColliderRecord floor(int64_t id,int64_t surface){ColliderRecord c;c.id=id;c.objectId=id;c.kind=Box;c.flags=Active|Enabled;c.layer=8;c.surfaceId=surface;c.surfaceFlags=surface?HasSurface:0;c.world[0]=c.world[5]=c.world[10]=1;return c;}
void near(float a,float b){CHECK(std::abs(a-b)<1e-6f);}
int main(){
 Bridge bridge;bridge.records.emplace(1,floor(1,10));bridge.records.emplace(2,floor(2,20));
 GroundState ground;GroundPlayerState player;GroundFrame frame;frame.deltaTime=.01f;frame.gameTime=12;
 ground.enterContact(bridge.records[1],false,player,bridge);CHECK(ground.touchingGround&&player.surface==10);CHECK(!ground.onGround);
 ground.updateState(frame,player,bridge);CHECK(ground.onGround);near(ground.lastGroundedTime,12);
 ground.enterContact(bridge.records[2],false,player,bridge);ground.enterContact(bridge.records[2],false,player,bridge);CHECK(ground.contacts.size()==2&&player.surface==20);
 ground.exitContact(bridge.records[2],player,bridge);CHECK(ground.contacts.size()==1&&player.surface==10);
 ground.forceOff();ground.forceOff();CHECK(!ground.onGround&&ground.forcedOff==2);ground.stopForceOff();CHECK(!ground.onGround);ground.stopForceOff();CHECK(ground.onGround);ground.stopForceOff();CHECK(ground.forcedOff==-1&&ground.onGround);
 bridge.records[1].flags&=~Enabled;frame.gameTime=13;ground.updateState(frame,player,bridge);CHECK(ground.onGround&&!ground.touchingGround&&ground.contacts.empty()&&player.surface==0);near(ground.lastGroundedTime,13);
 frame.gameTime=14;ground.updateState(frame,player,bridge);CHECK(!ground.onGround);near(ground.lastGroundedTime,13);
 auto slippery=floor(3,0);slippery.groundTags=SlipperyTag;CHECK(!GroundState::checkable(slippery));
 auto onlyPlayer=floor(4,0);onlyPlayer.layer=18;CHECK(!GroundState::checkable(onlyPlayer));onlyPlayer.groundTags=FloorTag;CHECK(GroundState::checkable(onlyPlayer));onlyPlayer.flags|=Trigger;CHECK(!GroundState::checkable(onlyPlayer));
 auto moving=floor(5,30);moving.bodyId=42;moving.groundTags=MovingTag;bridge.records[5]=moving;
 ground.enterContact(moving,false,player,bridge);CHECK(bridge.attaches==1&&bridge.tracked(5));ground.exitContact(moving,player,bridge);CHECK(bridge.detaches==1&&!bridge.tracked(5));
 GroundState slope;slope.slopeCheck=true;slope.enterContact(moving,false,player,bridge);CHECK(bridge.attaches==1);
 GroundState portal;portal.enterContact(moving,true,player,bridge);CHECK(portal.contacts.size()==1&&!portal.touchingGround&&bridge.attaches==1);
 auto enemy=floor(6,0);enemy.layer=12;bridge.records[6]=enemy;ground.enterContact(enemy,false,player,bridge);CHECK(ground.canJump&&ground.currentEnemy==6);ground.updateState(frame,player,bridge);CHECK(ground.canJump);bridge.records[6].world[3]=40.001f;ground.updateState(frame,player,bridge);CHECK(!ground.canJump);
 ground.superJumpChance=.005f;player.stillHolding=true;player.slamForce=3;frame.hasShockwave=true;ground.updateState(frame,player,bridge);
 CHECK(bridge.shockwaves==1&&!player.stillHolding);near(bridge.shockwaveMultiplier,6.75f);near(ground.extraJumpChance,.296f);near(player.slamForce,3);
 ground.extraJumpChance=.005f;ground.updateState(frame,player,bridge);near(player.slamForce,0);
 ground.bounceChance=.005f;ground.heavyFall=true;ground.hasImpacted=true;player.slamForce=4;frame.jumpHeld=true;ground.updateState(frame,player,bridge);CHECK(bridge.bounces==1&&!ground.heavyFall&&ground.hasImpacted);near(player.slamForce,0);ground.updateState(frame,player,bridge);CHECK(!ground.hasImpacted);
 GroundState inactive;inactive.active=false;inactive.onGround=true;inactive.canJump=true;inactive.heavyFall=true;inactive.lastGroundedTime=100;
 GroundState other;other.lastGroundedTime=7;other.forcedOff=3;other.superJumpChance=.2f;
 ground.lastGroundedTime=13;ground.forcedOff=1;GroundGroup group;group.add(nullptr);group.add(&ground);group.add(&inactive);group.add(&other);
 CHECK(group.onGround());CHECK(!group.canJump()&&!group.heavyFall());near(group.sinceLastGrounded(),13);CHECK(group.forcedOff()==3);near(group.superJumpChance(),.2f);
 group.setHeavyFall(true);CHECK(inactive.heavyFall&&group.heavyFall());group.setBounceChance(.4f);near(inactive.bounceChance,.4f);
 group.add(&other);group.remove(&other);CHECK(group.instances.size()==3);group.remove(nullptr);
 GroundGroup empty;CHECK(empty.forcedOff()==std::numeric_limits<int>::min());near(empty.sinceLastGrounded(),-std::numeric_limits<float>::max());
 ground.disable(player);CHECK(!ground.slopeCheck&&!ground.onGround&&ground.forcedOff==0&&ground.contacts.empty()&&player.surface==0);
 std::puts("PASS: ground contacts/surface priority, filtering, support parenting, portal rejection, force-off counts, expiry/bounce event order, stale-contact update ordering and exact group timestamp/active-instance semantics");
}
