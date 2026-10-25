#include "nx_activation.h"
#include <cstdio>
#include <cstdlib>
using namespace nx;using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
GraphPack graphFixture(){
 GraphPack p;auto string=[&](const char*s){uint32_t at=uint32_t(p.strings.size()),size=uint32_t(std::strlen(s));p.strings.insert(p.strings.end(),s,s+size);return std::pair<uint32_t,uint32_t>{at,size};};
 for(unsigned i=0;i<2;i++){
  GraphNode n{};n.objectId=10+i;n.transformId=1000+i*100;n.parent=i?0:-1;n.firstComponent=uint32_t(p.components.size());n.componentCount=4;n.flags=i?0:1;n.layer=8;n.rotation[3]=1;n.scale={1,1,1};n.position={i?5.f:0,0,0};auto name=string(i?"Room":"Root");n.nameOffset=name.first;n.nameLength=name.second;
  for(unsigned c=0;c<4;c++){auto type=string(c==0?"Transform":(c==1?"BoxCollider":(c==2?"MonoBehaviour":"AudioSource")));p.components.push_back({n.transformId+c,i,type.first,type.second,0,0,c?3u:1u});}p.nodes.push_back(n);
 }return p;
}
PhysicsPack physicsFixture(){
 PhysicsPack p;auto&v=p.player;v.objectId=1;v.position={0,5,0};v.up={0,1,0};v.forward={0,0,1};v.walkSpeed=750;v.airAcceleration=6000;v.jumpPower=90;v.wallJumpPower=150;v.mass=100;v.radius=.5f;v.height=3.5f;v.center={0,.25f,0};v.gravity={0,-40,0};p.layerMasks.fill(~0u);
 ColliderRecord floor;floor.id=1001;floor.objectId=10;floor.kind=Box;floor.layer=8;floor.flags=Active|Enabled;floor.world[0]=floor.world[5]=floor.world[10]=1;floor.center[1]=-.5f;floor.size[0]=floor.size[2]=100;floor.size[1]=1;p.colliders.push_back(floor);
 auto wall=floor;wall.id=1101;wall.objectId=11;wall.flags=Enabled;wall.world[3]=5;wall.center[1]=2.5f;wall.size[0]=1;wall.size[1]=wall.size[2]=5;p.colliders.push_back(wall);return p;
}
int main(){
 std::string error;auto p=graphFixture();WorldGraph graph;CHECK(graph.initialize(p,error));auto definitions=physicsFixture();PhysicsWorld physics;CHECK(physics.initialize(definitions,0,error));SceneActivation scene(p,graph,physics);ActivationCounts counts;
 CHECK(scene.flush(counts,error)&&counts.staticStates==0&&counts.pending==0);CHECK(graph.setActive(1,true,error));CHECK(scene.flush(counts,error));CHECK(counts.staticStates==1&&counts.pending==2);CHECK(graph.activationEvents().empty()&&physics.staticColliderActive(1101));CHECK(scene.pendingRequests()[0].backend==PendingBackend::Gameplay&&scene.pendingRequests()[1].backend==PendingBackend::Audio);scene.clearPendingRequests();
 CHECK(graph.setComponentEnabled(5,false,error));CHECK(scene.flush(counts,error));CHECK(counts.staticStates==1&&counts.pending==0&&!physics.staticColliderActive(1101));CHECK((physics.colliderMetadata(1101)->flags&Active)&&!(physics.colliderMetadata(1101)->flags&Enabled));
 CHECK(graph.setComponentEnabled(5,true,error));CHECK(scene.flush(counts,error));CHECK(physics.staticColliderActive(1101));
 CHECK(graph.setActive(1,false,error));CHECK(scene.flush(counts,error));scene.clearPendingRequests();CHECK(graph.setActive(1,true,error));CHECK(graph.setActive(1,false,error));CHECK(scene.flush(counts,error));CHECK(counts.staticStates==1&&counts.pending==4&&!physics.staticColliderActive(1101));scene.clearPendingRequests();
 CHECK(graph.setWorldPosition(1,{9,0,0},error));CHECK(graph.setActive(1,true,error));CHECK(scene.flush(counts,error));CHECK(counts.staticStates==0&&counts.pending==3);CHECK(scene.pendingRequests()[0].backend==PendingBackend::StaticTransform);CHECK(!physics.staticColliderActive(1101));scene.clearPendingRequests();
 // A failed lazy shape creation rolls earlier body changes back, retaining
 // hierarchy requests for a later retry rather than reporting success.
 // Zero-size boxes are valid in Unity (epsilon extents). Use a zero-radius sphere,
 // which the current adapter rejects, to exercise rollback.
 auto badGraph=p;badGraph.nodes[0].flags=0;badGraph.nodes[1].flags=1;WorldGraph retryGraph;CHECK(retryGraph.initialize(badGraph,error));auto badPhysics=definitions;for(auto&c:badPhysics.colliders)c.flags=Enabled;badPhysics.colliders[1].kind=Sphere;badPhysics.colliders[1].radius=0;PhysicsWorld retryPhysics;CHECK(retryPhysics.initialize(badPhysics,0,error));SceneActivation retry(badGraph,retryGraph,retryPhysics);
 CHECK(retryGraph.setActive(0,true,error));CHECK(!retry.flush(counts,error));CHECK(!retryPhysics.staticColliderActive(1001)&&!retryPhysics.staticColliderActive(1101));CHECK(!(retryPhysics.colliderMetadata(1001)->flags&Active));CHECK(!retryGraph.activationEvents().empty()&&retry.pendingRequests().empty());
 auto fixed=definitions;for(auto&c:fixed.colliders)c.flags=Enabled;CHECK(retryPhysics.initialize(fixed,0,error));CHECK(retry.flush(counts,error));CHECK(counts.staticStates==2&&counts.pending==4);CHECK(retryPhysics.staticColliderActive(1001)&&retryPhysics.staticColliderActive(1101));CHECK(retryGraph.activationEvents().empty());
 std::puts("PASS: graph/component activation reaches static physics; final-state coalescing, exact enabled flags, explicit audio/gameplay/transform deferral, failed-shape rollback and retry; other scene backends are not implemented");
}
