#include "nx_physics.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace nx;using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
void near(Vec3 a,Vec3 b){if(std::abs(a.x-b.x)>1e-5f||std::abs(a.y-b.y)>1e-5f||std::abs(a.z-b.z)>1e-5f){std::fprintf(stderr,"vector (%g,%g,%g), expected (%g,%g,%g)\n",a.x,a.y,a.z,b.x,b.y,b.z);CHECK(false);}}
void initialize(PhysicsWorld&w,float mass,unsigned workers=0){
 PhysicsPack p;auto&s=p.player;s.objectId=1;s.position={100,100,0};s.up={0,1,0};s.forward={0,0,1};s.walkSpeed=750;s.airAcceleration=6000;s.jumpPower=90;s.wallJumpPower=150;s.mass=100;s.radius=.5f;s.height=3.5f;s.gravity={0,-40,0};p.layerMasks.fill(~0u);
 ColliderRecord c{};c.id=300;c.objectId=2;c.bodyId=200;c.kind=Box;c.flags=Active|Enabled|AttachedBody;c.world[0]=c.world[5]=c.world[10]=1;c.world[7]=100;c.size[0]=c.size[1]=c.size[2]=1;p.colliders.push_back(c);
 GraphPack g;const char*types="TransformRigidbody";g.strings.assign(types,types+18);
 ActorPack a;std::memcpy(a.header.magic,"NXACTOR\0",8);a.header.version=1;a.header.bodyCount=2;a.header.colliderCount=1;
 for(unsigned i=0;i<2;i++){GraphNode n{};n.objectId=i+1;n.transformId=1000+i;n.parent=-1;n.flags=1;n.layer=i?0:2;n.firstComponent=g.components.size();n.componentCount=2;n.scale={1,1,1};n.rotation[3]=1;n.position=i?Vec3{0,100,0}:s.position;g.nodes.push_back(n);g.components.push_back({n.transformId,i,0,9,0,0,1});g.components.push_back({int64_t(100+i*100),i,9,9,0,0,1});ActorRecord b{};b.id=100+i*100;b.objectId=i+1;b.flags=ActorActive|ActorImplicitCenter|ActorImplicitTensor|(i?0u:ActorPlayer);b.layer=n.layer;b.position=n.position;b.rotation[3]=b.inertiaRotation[3]=1;b.mass=i?mass:100;b.inertia={1,1,1};b.colliderCount=i?1:0;a.bodies.push_back(b);}
 a.colliders={{0,1}};a.header.graphCrc=g.checksum();a.header.physicsCrc=physicsChecksum(p);std::string error;WorldGraph graph;CHECK(graph.initialize(g,error));CHECK(w.initialize(p,workers,error));CHECK(w.initializeActors(a,g,graph,error));
}
Vec3 velocity(PhysicsWorld&w){ActorPose p;std::string error;CHECK(w.actorPose(200,p,error));return p.velocity;}
void setVelocity(PhysicsWorld&w,Vec3 value){ActorPose p;std::string error;CHECK(w.actorPose(200,p,error));p.velocity=value;CHECK(w.setActorPose(200,p,error));}
void fixtures(){
 PhysicsWorld w;initialize(w,2);std::string error;CHECK(w.addActorForce(200,{2,4,6},ActorForceMode::Impulse,error));CHECK(w.addActorForce(200,{1,2,3},ActorForceMode::VelocityChange,error));near(velocity(w),{});CHECK(!w.addActorForce(200,{NAN,0,0},ActorForceMode::Force,error));CHECK(!w.addActorForce(200,{},ActorForceMode(9),error));CHECK(!w.addActorForce(999,{},ActorForceMode::Force,error));CHECK(!w.addActorForce(100,{1,0,0},ActorForceMode::Force,error));CHECK(!w.setActorMass(200,0,error));CHECK(!w.setActorMass(200,INFINITY,error));CHECK(w.actorMass(200)==2);CHECK(w.step(.008f,error));near(velocity(w),{2,4,6});CHECK(w.step(.008f,error));near(velocity(w),{2,4,6});
 CHECK(w.setActorGravity(200,true,error));CHECK(w.actorUsesGravity(200));CHECK(w.step(.008f,error));near(velocity(w),{2,3.68f,6});CHECK(w.setActorGravity(200,false,error));CHECK(w.step(.008f,error));near(velocity(w),{2,3.68f,6});
 CHECK(w.setActorActive(200,false,error));CHECK(w.setActorMass(200,7,error));CHECK(w.setActorGravity(200,true,error));CHECK(w.setActorActive(200,true,error));CHECK(w.actorMass(200)==7&&w.actorUsesGravity(200));CHECK(w.setActorGravity(200,false,error));
 // Queue entries are deduplicated even across a disable/re-enable in one tick.
 setVelocity(w,{});CHECK(w.addActorForce(200,{1,0,0},ActorForceMode::VelocityChange,error));CHECK(w.setActorActive(200,false,error));CHECK(w.setActorActive(200,true,error));CHECK(w.addActorForce(200,{2,0,0},ActorForceMode::VelocityChange,error));CHECK(w.step(.008f,error));near(velocity(w),{2,0,0});
 std::puts("PASS: native actor force accumulation, invalid-call isolation, mass/gravity changes, fixed-step consumption and disable/re-enable queue ownership");
}
void reference(const char*path){
 std::ifstream input(path);CHECK(input);unsigned count=0;
 float mass;uint32_t mode;std::string scenario;Vec3 immediate,after,second;
 while(input>>mass>>mode>>scenario>>immediate.x>>immediate.y>>immediate.z>>after.x>>after.y>>after.z>>second.x>>second.y>>second.z){
  for(unsigned workers:{0u,2u}){PhysicsWorld w;initialize(w,mass,workers);std::string error;setVelocity(w,{1,2,3});if(scenario=="inactive-call")CHECK(w.setActorActive(200,false,error));if(scenario=="kinematic-call")CHECK(w.setActorKinematic(200,true,error));CHECK(w.addActorForce(200,{10,20,-30},ActorForceMode(mode),error));
   if(scenario=="write-after")setVelocity(w,{4,5,6});if(scenario=="mass-after")CHECK(w.setActorMass(200,mass*2,error));if(scenario=="deactivate"){CHECK(w.setActorActive(200,false,error));CHECK(w.setActorActive(200,true,error));}if(scenario=="mode-cycle"){CHECK(w.setActorKinematic(200,true,error));CHECK(w.setActorKinematic(200,false,error));}if(scenario=="inactive-call")CHECK(w.setActorActive(200,true,error));if(scenario=="kinematic-call")CHECK(w.setActorKinematic(200,false,error));
   std::printf("reference mass=%g mode=%u scenario=%s workers=%u\n",mass,mode,scenario.c_str(),workers);near(velocity(w),immediate);CHECK(w.step(.008f,error));near(velocity(w),after);CHECK(w.step(.008f,error));near(velocity(w),second);
  }count++;
 }CHECK(input.eof()&&count==56);std::puts("PASS: 56 measured Unity 2022.3 force/mass/velocity/lifecycle cases match with 0 and 2 workers; angular/off-center forces and full gameplay remain pending");
}
int main(int argc,char**argv){fixtures();if(argc==2)reference(argv[1]);else if(argc!=1)return 2;}
