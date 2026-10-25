#include "nx_physics.h"
#include "nx_actor_scene.h"
#include "nx_activation.h"
#include <cstdio>
#include <cstdlib>
using namespace nx;using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b,float tolerance=.001f){if(std::abs(a-b)>tolerance){std::fprintf(stderr,"values %.9g %.9g\n",a,b);CHECK(false);}}
GraphPack graphFixture(){
 GraphPack g;const char*types="TransformRigidbody";g.strings.assign(types,types+18);
 for(uint32_t i=0;i<5;i++){
  GraphNode n{};n.objectId=i?9+i:1;n.transformId=1000+i;n.parent=i>1?1:-1;n.firstComponent=g.components.size();n.componentCount=i<2?2:1;n.flags=i==1?0:1;n.rotation[3]=1;n.scale={1,1,1};n.layer=i==2?8:i==3?9:i==4?10:i==0?2:0;n.position=i==0?Vec3{50,5,0}:i==1?Vec3{10,5,0}:i==3?Vec3{0,0,2}:Vec3{0,0,-2};if(i==1){n.rotation[1]=n.rotation[3]=std::sqrt(.5f);}g.nodes.push_back(n);g.components.push_back({n.transformId,i,0,9,0,0,1});if(i<2)g.components.push_back({i==0?100:200,i,9,9,0,0,1});
 }return g;
}
PhysicsPack physicsFixture(){
 PhysicsPack p;auto&s=p.player;s.objectId=1;s.position={50,5,0};s.up={0,1,0};s.forward={0,0,1};s.walkSpeed=750;s.airAcceleration=6000;s.jumpPower=90;s.wallJumpPower=150;s.mass=100;s.radius=.5f;s.height=3.5f;s.center={0,.25f,0};s.gravity={0,-40,0};p.layerMasks.fill(~0u);
 ColliderRecord floor{};floor.id=300;floor.objectId=2;floor.kind=Box;floor.layer=8;floor.flags=Active|Enabled;floor.world[0]=floor.world[5]=floor.world[10]=1;floor.center[1]=-.5f;floor.size[0]=floor.size[2]=100;floor.size[1]=1;p.colliders.push_back(floor);
 for(unsigned i=0;i<3;i++){ColliderRecord c{};c.id=400+i;c.objectId=11+i;c.bodyId=200;c.kind=Box;c.layer=8+i;c.flags=Enabled|AttachedBody|(i==2?Trigger:0u);c.world[0]=c.world[5]=c.world[10]=1;c.world[3]=i==1?12:8;c.world[7]=5;c.size[0]=c.size[1]=c.size[2]=i==2?1.5f:1;p.colliders.push_back(c);}return p;
}
ActorPack actorFixture(const PhysicsPack&p,const GraphPack&g,bool kinematic=false){
 ActorPack a;std::memcpy(a.header.magic,"NXACTOR\0",8);a.header.version=1;a.header.bodyCount=2;a.header.colliderCount=3;a.header.graphCrc=g.checksum();a.header.physicsCrc=physicsChecksum(p);
 ActorRecord player{};player.id=100;player.objectId=1;player.layer=2;player.flags=ActorActive|ActorPlayer|ActorGravity|ActorImplicitCenter|ActorImplicitTensor;player.rotation[3]=player.inertiaRotation[3]=1;player.position={50,5,0};player.mass=100;player.inertia={1,1,1};a.bodies.push_back(player);
 ActorRecord body=player;body.id=200;body.objectId=10;body.layer=0;body.flags=ActorGravity|ActorImplicitCenter|ActorImplicitTensor|(kinematic?ActorKinematic:0u);body.position={10,5,0};body.rotation[1]=body.rotation[3]=std::sqrt(.5f);body.mass=10;body.constraints=112;body.colliderCount=3;a.bodies.push_back(body);a.colliders={{1,1},{2,1},{3,1}};return a;
}
void fixtures(){
 for(bool kinematic:{false,true}){
  auto p=physicsFixture();auto g=graphFixture();auto a=actorFixture(p,g,kinematic);WorldGraph graph;std::string error;CHECK(graph.initialize(g,error));CHECK(a.bind(p,g,graph,error));PhysicsWorld world;CHECK(world.initialize(p,0,error));CHECK(world.initializeActors(a,g,graph,error));CHECK(!world.actorActive(200));CHECK(world.coverage().actorBodies==0);CHECK(!world.ray({8,10,0},{0,-8,0},1u<<8).hit);
  CHECK(world.setActorActive(200,true,error));CHECK(world.actorActive(200));CHECK(world.coverage().actorBodies==1&&world.coverage().actorColliders==3);CHECK(world.colliderMetadata(400)->flags&Active);
  auto left=world.ray({8,10,0},{0,-8,0},1u<<8);CHECK(left.hit&&left.collider==400);near(left.point.y,5.5f);auto right=world.ray({12,10,0},{0,-8,0},1u<<9);CHECK(right.hit&&right.collider==401);CHECK(!world.ray({12,10,0},{0,-8,0},1u<<8).hit);auto sensor=world.ray({8,10,0},{0,-8,0},1u<<10,true);CHECK(sensor.hit&&sensor.collider==402);near(sensor.point.y,5.75f);CHECK(!world.ray({8,10,0},{0,-8,0},1u<<10,false).hit);
  RayHit sweep;CHECK(world.sphereCast({12,10,0},.1f,{0,-8,0},1u<<9,false,sweep,error));CHECK(sweep.hit&&sweep.collider==401);CHECK(world.sphereCast({12,10,0},.1f,{0,-8,0},1u<<8,false,sweep,error));CHECK(!sweep.hit);
  std::vector<int64_t> overlaps;CHECK(world.overlapCapsule({8,5,0},{12,5,0},.1f,~0u,true,overlaps,error));CHECK(overlaps.size()==3);CHECK(world.overlapCapsule({8,5,0},{12,5,0},.1f,1u<<9,true,overlaps,error));CHECK(overlaps.size()==1&&overlaps[0]==401);
  ActorPose pose;CHECK(world.actorPose(200,pose,error));near(pose.position.x,10);near(pose.position.y,5);pose.position={20,7,0};pose.velocity={0,0,0};CHECK(world.setActorPose(200,pose,error));CHECK(!world.ray({12,10,0},{0,-8,0},1u<<9).hit);CHECK(world.ray({22,10,0},{0,-8,0},1u<<9).collider==401);
  if(kinematic){float q[4]={0,std::sqrt(.5f),0,std::sqrt(.5f)};CHECK(world.moveActorKinematic(200,{20,8,0},q,.008f,error));CHECK(world.step(.008f,error));CHECK(world.actorPose(200,pose,error));near(pose.position.y,8,.003f);}else{for(unsigned i=0;i<200;i++)CHECK(world.step(.008f,error));CHECK(world.actorPose(200,pose,error));near(pose.position.y,.5f,.05f);CHECK(world.ray({18,3,0},{0,-2.8f,0},1u<<10,true).collider==402);CHECK(!world.moveActorKinematic(200,{0,0,0},pose.rotation,.008f,error));}
  CHECK(world.setActorActive(200,false,error));CHECK(!world.actorActive(200));CHECK(!(world.colliderMetadata(400)->flags&Active));CHECK(!world.ray({22,20,0},{0,-19,0},1u<<9).hit);CHECK(world.setActorActive(200,true,error));CHECK(world.coverage().actorBodies==1);CHECK(!world.setActorActive(100,false,error));CHECK(!world.setActorActive(-1,true,error));
  pose.position={NAN,0,0};CHECK(!world.setActorPose(200,pose,error));
 }
 auto p=physicsFixture();auto g=graphFixture();auto a=actorFixture(p,g);a.bodies[1].flags|=ActorHasJoint;WorldGraph graph;std::string error;CHECK(graph.initialize(g,error));PhysicsWorld world;CHECK(world.initialize(p,0,error));CHECK(world.initializeActors(a,g,graph,error));CHECK(!world.setActorActive(200,true,error));CHECK(!world.actorActive(200)&&world.coverage().actorBodies==0);
 // Compound broadphase accepts the actor, but the actual supporting child
 // must obey its own layer, rather than the body's root GameObject layer.
 for(bool collide:{false,true}){
  auto p=physicsFixture();p.colliders[0].world[3]=12;p.colliders[0].size[0]=2;auto g=graphFixture();auto a=actorFixture(p,g);WorldGraph graph;PhysicsWorld world;CHECK(graph.initialize(g,error));CHECK(world.initialize(p,0,error));CHECK(world.initializeActors(a,g,graph,error));CHECK(world.setLayerCollision(8,9,collide,error));CHECK(world.setActorActive(200,true,error));for(unsigned i=0;i<250;i++)CHECK(world.step(.008f,error));ActorPose pose;CHECK(world.actorPose(200,pose,error));if(collide)near(pose.position.y,.5f,.05f);else CHECK(pose.position.y<-10);
 }
 // A failed initial actor attachment leaves the existing world intact and
 // permits a corrected attachment without duplicate bodies or stale metadata.
 p=physicsFixture();g=graphFixture();g.nodes[1].flags=1;for(size_t i=1;i<p.colliders.size();i++)p.colliders[i].flags|=Active;a=actorFixture(p,g);a.bodies[1].flags|=ActorActive|ActorHasJoint;WorldGraph initialGraph;PhysicsWorld initialWorld;CHECK(initialGraph.initialize(g,error));CHECK(initialWorld.initialize(p,0,error));CHECK(!initialWorld.initializeActors(a,g,initialGraph,error));CHECK(initialWorld.coverage().actorBodies==0&&!initialWorld.hasActor(100));CHECK(initialWorld.ray({0,3,0},{0,-4,0}).collider==300);a.bodies[1].flags&=~uint32_t(ActorHasJoint);CHECK(initialWorld.initializeActors(a,g,initialGraph,error));CHECK(initialWorld.coverage().actorBodies==1);
 std::puts("PASS: rotated authored compound bodies, per-child ray/sweep/overlap layer and collider identity, separate trigger following, dynamic landing, kinematic targets, inactive caching and explicit joint deferral");
}
void actual(const char*actors,const char*physics,const char*hierarchy){
 ActorPack a;PhysicsPack p;GraphPack g;WorldGraph graph;std::string error;const char*e=nullptr;CHECK(a.load(actors,error));CHECK(p.load(physics,&e));CHECK(g.load(hierarchy,error));CHECK(graph.initialize(g,error));if(!a.bind(p,g,graph,error)){std::fprintf(stderr,"bind: %s\n",error.c_str());CHECK(false);}CHECK(a.bodies.size()==1450&&a.colliders.size()==1715);
 PhysicsWorld world;CHECK(world.initialize(p,2,error));if(!world.initializeActors(a,g,graph,error)){std::fprintf(stderr,"initialize actors: %s\n",error.c_str());CHECK(false);}CHECK(world.coverage().actorBodies==4&&world.coverage().actorColliders==11);CHECK(world.coverage().dynamicDeferred==5);
 for(unsigned i=0;i<125;i++)CHECK(world.step(.008f,error));
 for(const auto&b:a.bodies)if(b.flags&ActorActive){ActorPose pose;CHECK(world.actorPose(b.id,pose,error));if(!(b.flags&ActorPlayer)){near(pose.position.x,b.position.x);near(pose.position.y,b.position.y);near(pose.position.z,b.position.z);}}
 auto changed=a;changed.header.physicsCrc^=1;CHECK(!changed.bind(p,g,graph,error));changed=a;changed.colliders[0].relativeActive^=1;CHECK(!changed.bind(p,g,graph,error));changed=a;changed.bodies[0].mass=0;CHECK(!changed.validate(error));
 std::puts("PASS: actual level 1-2 binds 1450 rigid bodies/1715 colliders; four initial kinematic actors instantiate eleven original trigger colliders; player helpers, joints and scene-script lifecycle still pending");
}
void graphBinding(){
 for(bool kinematic:{false,true}){
  auto p=physicsFixture();auto g=graphFixture();auto a=actorFixture(p,g,kinematic);WorldGraph graph;PhysicsWorld physics;ActorScene bridge;std::string error;CHECK(graph.initialize(g,error));CHECK(physics.initialize(p,0,error));CHECK(bridge.initialize(a,g,graph,physics,error));CHECK(graph.setWorldPosition(1,{20,7,0},error));CHECK(graph.setActive(1,true,error));CHECK(bridge.beforeStep(.008f,error));CHECK(physics.actorActive(200));ActorPose pose;CHECK(physics.actorPose(200,pose,error));near(pose.position.x,20);near(pose.position.y,7);CHECK(physics.step(.008f,error));CHECK(bridge.afterStep(error));
  SceneActivation activation(g,graph,physics);ActivationCounts counts;CHECK(activation.flush(counts,error));CHECK(counts.actorStates==1&&counts.pending==0&&graph.activationEvents().empty());
  if(kinematic){float q[4]={0,0,0,1};CHECK(graph.setWorldPose(1,{25,10,3},q,error));CHECK(bridge.beforeStep(.008f,error));CHECK(physics.step(.008f,error));CHECK(bridge.afterStep(error));CHECK(physics.actorPose(200,pose,error));near(pose.position.x,25,.003f);near(pose.position.y,10,.003f);}else{for(unsigned i=0;i<100;i++){CHECK(bridge.beforeStep(.008f,error));CHECK(physics.step(.008f,error));CHECK(bridge.afterStep(error));}CHECK(physics.actorPose(200,pose,error));near(graph.node(1)->world.m[13],pose.position.y);CHECK(pose.position.y<1);}
  CHECK(graph.setActive(1,false,error));CHECK(bridge.beforeStep(.008f,error));CHECK(!physics.actorActive(200));CHECK(activation.flush(counts,error));CHECK(counts.actorStates==1&&counts.pending==0);
  CHECK(graph.setActive(1,true,error));CHECK(bridge.beforeStep(.008f,error));const auto child=graph.node(2)->local;CHECK(graph.setLocal(2,child.position,child.rotation,{2,1,1},error));CHECK(!bridge.beforeStep(.008f,error));CHECK(error.find("shape rebuilding")!=std::string::npos);CHECK(graph.setLocal(2,child.position,child.rotation,child.scale,error));CHECK(bridge.beforeStep(.008f,error));CHECK(graph.setLayer(2,17,error));CHECK(!bridge.beforeStep(.008f,error));CHECK(error.find("layer mutation")!=std::string::npos);
 }
 auto g=graphFixture();g.nodes[1].parent=0;WorldGraph graph;std::string error;CHECK(graph.initialize(g,error));float q[4]={0,0,std::sqrt(.5f),std::sqrt(.5f)};CHECK(graph.setLocal(0,{3,4,5},q,{2,3,4},error));float target[4]={0,std::sqrt(.5f),0,std::sqrt(.5f)};CHECK(graph.setWorldPose(1,{1,2,3},target,error));near(graph.node(1)->world.m[12],1);near(graph.node(1)->world.m[13],2);near(graph.node(1)->world.m[14],3);float actual[4];CHECK(graph.worldRotation(1,actual,error));for(unsigned i=0;i<4;i++)near(actual[i],target[i]);CHECK(graph.setLocal(0,{3,4,5},q,{0,3,4},error));auto before=graph.node(1)->local;CHECK(!graph.setWorldPose(1,{1,2,3},target,error));CHECK(!std::memcmp(&before,&graph.node(1)->local,sizeof before));
 std::puts("PASS: hierarchy actor activation, kinematic graph targets, dynamic pose writeback, routed body events and world-pose conversion under rotated nonuniform parents with singular rollback");
}
void corrupt(const char*path){
 FILE*f=std::fopen(path,"rb");CHECK(f);std::fseek(f,0,SEEK_END);long length=std::ftell(f);std::rewind(f);std::vector<uint8_t> bytes(length);CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());std::fclose(f);
 auto reject=[](const std::vector<uint8_t>&data){FILE*f=std::tmpfile();CHECK(f);CHECK(std::fwrite(data.data(),1,data.size(),f)==data.size());std::rewind(f);ActorPack p;std::string error;CHECK(!p.read(f,error));std::fclose(f);};auto refresh=[](std::vector<uint8_t>&data){reinterpret_cast<ActorHeader*>(data.data())->crc=crc32(data.data()+40,data.size()-40);};
 auto bad=bytes;bad.pop_back();reject(bad);bad=bytes;bad.push_back(0);reject(bad);bad=bytes;reinterpret_cast<ActorHeader*>(bad.data())->bodyCount=UINT32_MAX;reject(bad);
 for(unsigned i=0;i<24;i++){bad=bytes;bad[40+(i*104729)%(bad.size()-40)]^=1;reject(bad);}bad=bytes;reinterpret_cast<ActorRecord*>(bad.data()+40)->mass=NAN;refresh(bad);reject(bad);bad=bytes;reinterpret_cast<ActorRecord*>(bad.data()+40)->rotation[3]=2;refresh(bad);reject(bad);
 std::puts("PASS: actor pack exact-length/count budgets, checksum mutations and refreshed-checksum invalid mass/quaternion rejection");
}
int main(int argc,char**argv){fixtures();graphBinding();if(argc==4){actual(argv[1],argv[2],argv[3]);corrupt(argv[1]);}else if(argc!=1)return 2;return 0;}
