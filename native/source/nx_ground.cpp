#include "nx_ground.h"
#include <algorithm>
#include <cmath>
namespace nx::game {
namespace {
float towardsZero(float value,float delta){return std::abs(value)<=delta?0:value+(value>=0?-delta:delta);}
bool movingSupport(const ColliderRecord&c){return (c.groundTags&MovingTag)||c.layer==11||c.layer==26;}
template<class Function>float maxActive(const GroundGroup&group,Function value){float result=-std::numeric_limits<float>::max();for(auto*s:group.instances)if(s->active&&s->enabled)result=std::max(result,value(*s));return result;}
}
bool GroundState::checkable(const ColliderRecord&c){
 if((c.flags&Trigger)||(c.groundTags&SlipperyTag))return false;
 if((EnvironmentLayers&(1u<<c.layer))||c.layer==11||c.layer==26)return true;
 return c.layer==18 && (c.groundTags&FloorTag);
}
bool GroundState::usable(const ColliderRecord*c){return c&&(c->flags&Enabled)&&!(c->flags&Trigger)&&(c->flags&Active)&&c->layer!=17&&c->layer!=10;}
void GroundState::enterContact(const ColliderRecord&c,bool portalBlocked,GroundPlayerState&p,GroundBridge&bridge){
 auto found=std::find(contacts.begin(),contacts.end(),c.id);
 if(checkable(c)&&found==contacts.end()){
  contacts.push_back(c.id);touchingGround=!portalBlocked;
  if(touchingGround){p.surface=c.surfaceId;if(!slopeCheck&&movingSupport(c)&&c.bodyId&&!bridge.tracked(c.id))bridge.attach(c.id);}
 }else if(!(c.groundTags&SlipperyTag)&&c.layer==12){currentEnemy=c.id;canJump=true;}
}
void GroundState::exitContact(const ColliderRecord&c,GroundPlayerState&p,GroundBridge&bridge){
 auto found=std::find(contacts.begin(),contacts.end(),c.id);
 if(!checkable(c)||found==contacts.end())return;
 bool last=found+1==contacts.end();contacts.erase(found);
 if(last){
  while(!contacts.empty()){auto*other=bridge.collider(contacts.back());if(usable(other)){p.surface=other->surfaceId;break;}contacts.pop_back();}
 }
 if(contacts.empty()){touchingGround=false;p.surface=0;}
 if(!slopeCheck&&movingSupport(c)&&bridge.tracked(c.id))bridge.detach(c.id);
}
void GroundState::updateState(const GroundFrame&f,GroundPlayerState&p,GroundBridge&bridge){
 if(forcedOff>0)onGround=false;else if(onGround!=touchingGround)onGround=touchingGround;
 if(!heavyFall)hitEnemies.clear();
 if(onGround)lastGroundedTime=float(f.gameTime);
 if(superJumpChance>0){
  superJumpChance=towardsZero(superJumpChance,f.deltaTime);
  if(superJumpChance==0){if(f.hasShockwave&&p.stillHolding)bridge.shockwave(p.slamForce*2.25f);extraJumpChance=.306f;p.stillHolding=false;}
 }
 if(extraJumpChance>0){extraJumpChance=towardsZero(extraJumpChance,f.deltaTime);if(extraJumpChance<=0&&superJumpChance<=0&&bounceChance<=0)p.slamForce=0;}
 if(bounceChance>0){
  bounceChance=towardsZero(bounceChance,f.deltaTime);
  if(!f.inputLocked&&f.jumpHeld){bounceChance=0;bridge.bounce(*this,bouncePosition,p);}
  if(!heavyFall&&extraJumpChance<=0&&superJumpChance<=0&&bounceChance<=0)p.slamForce=0;
 }else hasImpacted=false;
 for(size_t i=contacts.size();i>0;i--)if(!usable(bridge.collider(contacts[i-1])))contacts.erase(contacts.begin()+i-1);
 // Preserve the original ordering: onGround is updated before invalid contacts
 // are pruned. The resulting one-update delay is observable by game scripts.
 if(touchingGround&&contacts.empty()){touchingGround=false;p.surface=0;}
 if(canJump){auto*enemy=bridge.collider(currentEnemy);bool invalid=!enemy||!(enemy->flags&Active);if(!invalid){auto offset=enemy->point({0,0,0})-f.position;invalid=std::sqrt(dot(offset,offset))>40;}if(invalid)canJump=false;}
}
void GroundState::disable(GroundPlayerState&p){
 p.surface=0;contacts.clear();slopeCheck=false;onGround=false;touchingGround=false;canJump=false;heavyFall=false;instakillStomp=false;superJumpChance=0;extraJumpChance=0;bounceChance=0;hasImpacted=false;lastGroundedTime=0;forcedOff=0;
}
void GroundGroup::remove(GroundState*s){if(!s)return;auto it=std::find(instances.begin(),instances.end(),s);if(it!=instances.end())instances.erase(it);}
bool GroundGroup::onGround()const{for(auto*s:instances)if(s->onGround)return true;return false;}
bool GroundGroup::touchingGround()const{for(auto*s:instances)if(s->touchingGround)return true;return false;}
bool GroundGroup::heavyFall()const{for(auto*s:instances)if(s->active&&s->enabled&&s->heavyFall)return true;return false;}
bool GroundGroup::canJump()const{for(auto*s:instances)if(s->active&&s->enabled&&s->canJump)return true;return false;}
float GroundGroup::sinceLastGrounded()const{return maxActive(*this,[](const auto&s){return s.lastGroundedTime;});}
int GroundGroup::forcedOff()const{int value=std::numeric_limits<int>::min();for(auto*s:instances)if(s->active&&s->enabled)value=std::max(value,s->forcedOff);return value;}
float GroundGroup::superJumpChance()const{return maxActive(*this,[](const auto&s){return s.superJumpChance;});}
float GroundGroup::bounceChance()const{return maxActive(*this,[](const auto&s){return s.bounceChance;});}
float GroundGroup::extraJumpChance()const{return maxActive(*this,[](const auto&s){return s.extraJumpChance;});}
void GroundGroup::setImpacted(bool value){for(auto*s:instances)s->hasImpacted=value;}
void GroundGroup::setHeavyFall(bool value){for(auto*s:instances)s->heavyFall=value;}
void GroundGroup::setBounceChance(float value){for(auto*s:instances)s->bounceChance=value;}
void GroundGroup::forceOff(){for(auto*s:instances)s->forceOff();}
void GroundGroup::stopForceOff(){for(auto*s:instances)s->stopForceOff();}
void GroundGroup::update(const GroundFrame&f,GroundPlayerState&p,GroundBridge&bridge){for(auto*s:instances)if(s->active&&s->enabled)s->updateState(f,p,bridge);}
}
