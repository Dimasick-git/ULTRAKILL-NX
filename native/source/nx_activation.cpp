#include "nx_activation.h"
namespace nx::game {
namespace {
bool sameWorld(const Mat4&m,const ColliderRecord&c){for(unsigned r=0;r<3;r++)for(unsigned col=0;col<4;col++)if(std::abs(m.m[col*4+r]-c.world[r*4+col])>.001f)return false;return true;}
PendingBackend backend(const std::string&type){
 if(type=="Rigidbody"||type=="CharacterJoint"||type=="FixedJoint"||type=="ConfigurableJoint")return PendingBackend::DynamicPhysics;
 if(type.find("Renderer")!=std::string::npos||type=="Camera"||type=="Light"||type=="ParticleSystem"||type=="Canvas"||type=="CanvasGroup")return PendingBackend::Rendering;
 if(type.find("Audio")==0)return PendingBackend::Audio;
 if(type=="MonoBehaviour"||type=="NavMeshAgent"||type=="NavMeshObstacle")return PendingBackend::Gameplay;
 return PendingBackend::Unknown;
}
}
bool SceneActivation::flush(ActivationCounts&counts,std::string&error){
 counts={};staged.clear();changes.clear();seen.clear();
 const auto&events=graph.activationEvents();if(pending.size()+events.size()>1048576){error="pending scene backends are not consuming activation requests";return false;}
 for(const auto&event:events){
  if(event.component>=definitions.components.size()){error="invalid scene activation component";return false;}
  const auto&component=definitions.components[event.component];const auto*node=graph.node(component.node);
  if(!node||component.node>=definitions.nodes.size()||node->local.objectId!=definitions.nodes[component.node].objectId){error="scene activation ownership mismatch";return false;}
  std::string type=definitions.text(component.typeOffset,component.typeLength);
  if(type=="Transform"||type=="RectTransform"||type=="MeshFilter")continue;
  if(type=="Rigidbody"&&physics.hasActor(component.id)&&physics.actorActive(component.id)==node->active){counts.actorStates++;continue;}
  if(type=="CharacterJoint"||type=="FixedJoint"||type=="ConfigurableJoint"){NativeJointState state;std::string pending;if(physics.jointState(component.id,state,pending)){if(!state.broken&&!physics.setJointEnabled(component.id,graph.componentActive(event.component),error))return false;counts.jointStates++;continue;}}
  const auto*collider=physics.colliderMetadata(component.id);
  if(!collider){staged.push_back({event,backend(type)});continue;}
  if(collider->objectId!=node->local.objectId){error="scene/physics collider ownership mismatch";return false;}
  if(collider->flags&AttachedBody){if(physics.hasActor(collider->bodyId)&&bool(collider->flags&Active)==node->active&&bool(collider->flags&Enabled)==graph.componentEnabled(event.component)){counts.actorStates++;continue;}staged.push_back({event,PendingBackend::DynamicPhysics});continue;}
  if(graph.componentActive(event.component)&&!sameWorld(node->world,*collider)){staged.push_back({event,PendingBackend::StaticTransform});continue;}
  // Physics observes the final state before simulation. Script/renderer/audio
  // requests retain every transition in order, including transient toggles.
  if(seen.insert(collider->id).second)changes.push_back({collider->id,node->active,graph.componentEnabled(event.component),bool(collider->flags&Active),bool(collider->flags&Enabled)});
 }
 size_t applied=0;
 for(;applied<changes.size();applied++){
  const auto&change=changes[applied];
  if(!physics.setStaticColliderState(change.collider,change.hierarchyActive,change.enabled,error)){
   std::string original=error,rollback;
   while(applied){const auto&previous=changes[--applied];if(!physics.setStaticColliderState(previous.collider,previous.previousActive,previous.previousEnabled,rollback)){error=original+"; static activation rollback failed: "+rollback;return false;}}
   error=original;return false;
  }
 }
 pending.insert(pending.end(),staged.begin(),staged.end());counts.staticStates=unsigned(changes.size());counts.pending=unsigned(staged.size());graph.clearEvents();error.clear();return true;
}
}
