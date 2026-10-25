#pragma once
#include "nx_physics_pack.h"
#include <vector>
#include <limits>

namespace nx::game {
constexpr uint32_t EnvironmentLayers=(1u<<6)|(1u<<7)|(1u<<8)|(1u<<24);
struct GroundState;
struct GroundPlayerState {bool stillHolding=false;float slamForce=0;int64_t surface=0;};
struct GroundBridge {
 virtual ~GroundBridge()=default;
 virtual const ColliderRecord* collider(int64_t id)const=0;
 virtual bool tracked(int64_t id)const=0;
 virtual void attach(int64_t id)=0;
 virtual void detach(int64_t id)=0;
 virtual void shockwave(float force)=0; // includes camera shake/debris effects
 virtual void bounce(GroundState&,Vec3 position,GroundPlayerState&)=0;
};
struct GroundFrame {
 float deltaTime=0;double gameTime=0;
 Vec3 position{};
 bool inputLocked=false,jumpHeld=false,hasShockwave=false;
};
// GroundCheck contact bookkeeping and UpdateState. Damage/stomping, water,
// portal closest-point queries and engine trigger dispatch are separate adapters.
struct GroundState {
 bool active=true,enabled=true,slopeCheck=false;
 bool onGround=false,touchingGround=false,canJump=false,heavyFall=false,instakillStomp=false,hasImpacted=false;
 float superJumpChance=0,extraJumpChance=0,bounceChance=0;
 int forcedOff=0;float lastGroundedTime=0;
 Vec3 bouncePosition{};int64_t currentEnemy=0;
 std::vector<int64_t> contacts,hitEnemies;
 static bool checkable(const ColliderRecord&);
 static bool usable(const ColliderRecord*);
 void enterContact(const ColliderRecord&,bool portalBlocked,GroundPlayerState&,GroundBridge&);
 void exitContact(const ColliderRecord&,GroundPlayerState&,GroundBridge&);
 void updateState(const GroundFrame&,GroundPlayerState&,GroundBridge&);
 void forceOff(){onGround=false;forcedOff++;}
 void stopForceOff(){forcedOff--;if(forcedOff<=0)onGround=touchingGround;}
 void disable(GroundPlayerState&);
};
struct GroundGroup {
 std::vector<GroundState*> instances;Vec3 localPosition{};
 void add(GroundState*instance){if(instance)instances.push_back(instance);}
 void remove(GroundState*);
 bool onGround()const;
 bool touchingGround()const;
 bool heavyFall()const;
 bool canJump()const;
 float sinceLastGrounded()const; // original group returns the raw timestamp
 int forcedOff()const;
 float superJumpChance()const;
 float bounceChance()const;
 float extraJumpChance()const;
 void setImpacted(bool);
 void setHeavyFall(bool);
 void setBounceChance(float);
 void forceOff();void stopForceOff();
 void update(const GroundFrame&,GroundPlayerState&,GroundBridge&);
};
}
