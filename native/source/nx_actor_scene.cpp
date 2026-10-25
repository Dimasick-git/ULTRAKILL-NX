#include "nx_actor_scene.h"
#include <cfloat>
namespace nx::game {
namespace {
Mat4 relativeShape(const Mat4&world,const ActorPose&pose){
 float x=pose.rotation[0],y=pose.rotation[1],z=pose.rotation[2],w=pose.rotation[3];Vec3 columns[]={{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w)},{2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)},{2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)}};Mat4 result;result.m[15]=1;
 for(unsigned col=0;col<4;col++){Vec3 value{world.m[col*4],world.m[col*4+1],world.m[col*4+2]};if(col==3)value=value-pose.position;for(unsigned row=0;row<3;row++)result.m[col*4+row]=dot(columns[row],value);}return result;
}
float magnitude(const Mat4&world){return std::max({std::abs(world.m[12]),std::abs(world.m[13]),std::abs(world.m[14])});}
}
bool ActorScene::graphPose(uint32_t node,ActorPose&pose,std::string&error)const{const auto*n=graph->node(node);if(!n){error="actor hierarchy node missing";return false;}pose.position={n->world.m[12],n->world.m[13],n->world.m[14]};pose.velocity={};pose.angularVelocity={};return graph->worldRotation(node,pose.rotation,error);}
bool ActorScene::geometryUnchanged(const Binding&binding,const ActorPose&pose,std::string&error)const{
 for(const auto&shape:binding.shapes){const auto*node=graph->node(shape.node);if(shape.collider){const auto*collider=physics->colliderMetadata(shape.collider);if(!collider||collider->layer!=node->local.layer){error="actor child layer mutation needs the native collider-state adapter";return false;}}auto local=relativeShape(node->world,pose);for(unsigned col=0;col<4;col++)for(unsigned row=0;row<3;row++){unsigned i=col*4+row;float tolerance=.0001f*std::max(1.f,std::abs(shape.local.m[i]));if(col==3)tolerance+=16*FLT_EPSILON*std::max(shape.sourceMagnitude,magnitude(node->world));if(std::abs(local.m[i]-shape.local.m[i])>tolerance){error="actor collider scale/local transform mutation needs shape rebuilding: body "+std::to_string(pack->bodies[binding.actor].id);return false;}}}return true;
}
bool ActorScene::initialize(const ActorPack&p,const GraphPack&g,WorldGraph&w,PhysicsWorld&world,std::string&error){
 if(!world.initializeActors(p,g,w,error))return false;
 pack=&p;definitions=&g;graph=&w;physics=&world;bindings.clear();for(uint32_t i=0;i<p.bodies.size();i++){
  const auto&a=p.bodies[i];Binding b{i,w.find(a.objectId),{}};ActorPose pose;if(!graphPose(b.node,pose,error))return false;b.shapes.push_back({b.node,0,relativeShape(w.node(b.node)->world,pose),magnitude(w.node(b.node)->world)});
  for(size_t c=a.firstCollider;c<size_t(a.firstCollider)+a.colliderCount;c++){const auto*collider=world.colliderAt(p.colliders[c].index);if(!collider){error="actor collider index disappeared after binding";return false;}uint32_t node=w.find(collider->objectId);if(node==WorldGraph::Missing){error="actor collider graph binding disappeared";return false;}b.shapes.push_back({node,collider->id,relativeShape(w.node(node)->world,pose),magnitude(w.node(node)->world)});}
  bindings.emplace(a.id,std::move(b));
 }error.clear();return true;
}
bool ActorScene::beforeStep(float dt,std::string&error){
 if(!pack||!graph||!physics||!std::isfinite(dt)||std::abs(dt-.008f)>1e-7f){error="actor scene needs initialized 8 ms tick";return false;}
 dirty.clear();changes.clear();
 for(const auto&event:graph->activationEvents()){
  if(event.component>=definitions->components.size()){error="invalid actor scene component event";return false;}const auto&component=definitions->components[event.component];if(bindings.count(component.id))dirty.insert(component.id);
 }
 auto rollback=[&](const std::string&original){std::string restored;for(auto it=changes.rbegin();it!=changes.rend();++it){if(!physics->setActorActive(it->id,it->oldActive,restored)||!physics->setActorPose(it->id,it->oldPose,restored)){error=original+"; actor activation rollback failed: "+restored;return false;}}error=original;return false;};
 for(auto id:dirty){const auto&b=bindings.at(id);const auto&a=pack->bodies[b.actor];bool desired=graph->node(b.node)->active,old=physics->actorActive(id);if(desired==old)continue;if(a.flags&ActorPlayer){error="player active-state lifecycle is not implemented";return rollback(error);}ActorPose previous;if(!physics->actorPose(id,previous,error))return rollback(error);changes.push_back({id,old,previous});if(desired){ActorPose pose;if(!graphPose(b.node,pose,error)||!physics->setActorPose(id,pose,error))return rollback(error);}if(!physics->setActorActive(id,desired,error))return rollback(error);}
 for(auto id:physics->activeActorIds()){const auto&b=bindings.at(id);const auto&a=pack->bodies[b.actor];if(a.flags&ActorPlayer)continue;ActorPose target;if(!graphPose(b.node,target,error)||!geometryUnchanged(b,target,error))return false;if(physics->actorKinematic(id)&&!physics->moveActorKinematic(id,target.position,target.rotation,dt,error))return false;}
 error.clear();return true;
}
bool ActorScene::afterStep(std::string&error){
 if(!pack||!graph||!physics){error="actor scene not initialized";return false;}
 for(auto id:physics->activeActorIds()){const auto&b=bindings.at(id);const auto&a=pack->bodies[b.actor];if((a.flags&ActorPlayer)||physics->actorKinematic(id))continue;ActorPose pose;if(!physics->actorPose(id,pose,error)||!graph->setWorldPose(b.node,pose.position,pose.rotation,error))return false;}
 error.clear();return true;
}
}
