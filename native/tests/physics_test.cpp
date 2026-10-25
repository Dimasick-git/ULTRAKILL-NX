#include "nx_physics.h"
#include "nx_clock.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <chrono>
using namespace nx;
using namespace nx::game;
#define CHECK(condition) do{if(!(condition)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition);std::exit(1);}}while(false)
namespace {
void close(float actual,float expected,float tolerance){if(std::abs(actual-expected)>tolerance){std::fprintf(stderr,"actual %.9g expected %.9g tolerance %.9g\n",actual,expected,tolerance);std::exit(1);}}
PhysicsPack fixture(){
 PhysicsPack pack;auto&p=pack.player;p.objectId=1;p.position={0,5,0};p.up={0,1,0};p.forward={0,0,1};p.cameraOffset={0,1.4f,0};
 p.walkSpeed=750;p.airAcceleration=6000;p.jumpPower=90;p.wallJumpPower=150;p.mass=100;p.radius=.5f;p.height=3.5f;p.center={0,.25f,0};p.gravity={0,-40,0};p.frictionCombine=1;p.bounceCombine=1;
 pack.layerMasks.fill(~0u);
 ColliderRecord floor;floor.id=2;floor.objectId=2;floor.kind=Box;floor.layer=8;floor.flags=Active|Enabled;
 floor.world[0]=floor.world[5]=floor.world[10]=1;floor.center[1]=-.5f;floor.size[0]=floor.size[2]=100;floor.size[1]=1;floor.dynamicFriction=floor.staticFriction=.6f;pack.colliders.push_back(floor);
 ColliderRecord trigger=floor;trigger.id=3;trigger.objectId=3;trigger.flags|=Trigger;trigger.center[1]=1;trigger.size[0]=trigger.size[2]=2;trigger.size[1]=.1f;pack.colliders.push_back(trigger);
 ColliderRecord ground;ground.id=4;ground.objectId=4;ground.bodyId=11;ground.kind=Capsule;ground.layer=20;ground.flags=Active|Enabled|Trigger|AttachedBody;
 ground.world[0]=ground.world[10]=.85f;ground.world[5]=.8f;ground.world[7]=p.position.y-1.256f;ground.size[0]=1;ground.radius=.25f;ground.height=1.3f;pack.colliders.push_back(ground);p.groundCollider=4;
 auto slope=ground;slope.id=5;slope.objectId=5;slope.world[0]=slope.world[5]=slope.world[10]=1;slope.world[7]=p.position.y-.1f;slope.center[1]=.25f;slope.radius=.45f;slope.height=3.5f;pack.colliders.push_back(slope);p.slopeCollider=5;
 return pack;
}
void tick(PhysicsWorld&world,unsigned count){std::string error;for(unsigned i=0;i<count;i++){if(!world.step(.008f,error)){std::fprintf(stderr,"step: %s\n",error.c_str());CHECK(false);}}}
void initialized(PhysicsWorld&world,const PhysicsPack&pack,unsigned workers){std::string error;if(!world.initialize(pack,workers,error)){std::fprintf(stderr,"initialize: %s\n",error.c_str());CHECK(false);}}
Vec3 runAtFps(unsigned fps){
 auto pack=fixture();pack.player.position={0,1000,0};PhysicsWorld world;initialized(world,pack,0);FixedClock clock;
 for(uint64_t i=0;i<fps;i++){clock.advance((i+1)*1000000000/fps-i*1000000000/fps);clock.run(125,[&](float dt){MovementInput input;input.mass=100;input.walkSpeed=750;input.airAcceleration=6000;input.velocity=world.playerVelocity();input.inputDirection={0,0,1};MovementState state;world.applyMovement(move(input,state));std::string error;CHECK(world.step(dt,error));});}
 CHECK(clock.ticks==125);return world.playerPosition();
}
void fixtures(){
 for(unsigned workers:{0u,2u}){
  auto pack=fixture();PhysicsWorld world;initialized(world,pack,workers);auto coverage=world.coverage();CHECK(coverage.staticBodies==2 && coverage.sensors==1);
  auto hit=world.ray({0,10,0},{0,-20,0});CHECK(hit.hit && hit.collider==2);close(hit.point.y,0,.001f);close(hit.normal.y,1,.001f);
  hit=world.ray({0,10,0},{0,-20,0},1u<<8,true);CHECK(hit.hit && hit.collider==3);close(hit.point.y,1.05f,.001f);
  CHECK(!world.ray({0,10,0},{0,-20,0},1u<<9).hit);
  std::vector<int64_t> overlaps;std::string queryError;
  CHECK(world.overlapCapsule({0,.1f,0},{0,.4f,0},.25f,~0u,false,overlaps,queryError));CHECK(overlaps.size()==1 && overlaps[0]==2);
  CHECK(world.overlapCapsule({0,1,0},{0,1,0},.1f,~0u,true,overlaps,queryError));CHECK(overlaps.size()==1 && overlaps[0]==3);
  CHECK(world.overlapCapsule({0,1,0},{0,1,0},.1f,~0u,false,overlaps,queryError));CHECK(overlaps.empty());
  CHECK(world.overlapCapsule({0,50,0},{0,51,0},.25f,~0u,true,overlaps,queryError));CHECK(overlaps.empty());
  CHECK(!world.overlapCapsule({0,0,0},{0,0,0},-1,~0u,true,overlaps,queryError));
  tick(world,250);close(world.playerPosition().y,1.5f,.03f);close(world.playerVelocity().y,0,.01f);
  CHECK(world.groundOverlaps(false,false,overlaps,queryError));CHECK(std::find(overlaps.begin(),overlaps.end(),2)!=overlaps.end());
  CHECK(world.groundOverlaps(false,true,overlaps,queryError));CHECK(std::find(overlaps.begin(),overlaps.end(),2)!=overlaps.end());
  CHECK(world.groundOverlaps(true,false,overlaps,queryError));CHECK(std::find(overlaps.begin(),overlaps.end(),2)!=overlaps.end());
  MovementResult launch;launch.useGravity=false;launch.queuedVelocityChange={0,10,0};world.applyMovement(launch);close(world.playerVelocity().y,0,.01f);
  tick(world,1);close(world.playerVelocity().y,10,.001f);tick(world,1);close(world.playerVelocity().y,10,.001f);
  MovementResult force;force.useGravity=false;force.queuedForce={1000,0,0};world.applyMovement(force);close(world.playerVelocity().x,0,.001f);tick(world,1);close(world.playerVelocity().x,.08f,.001f);tick(world,1);close(world.playerVelocity().x,.08f,.001f);
  PhysicsWorld fast;initialized(fast,pack,workers);fast.setPlayerVelocity({0,-1000,0});tick(fast,2);CHECK(fast.playerPosition().y>=1.3f);
 }
 auto pack=fixture();pack.layerMasks[2]&=~(1u<<8);pack.layerMasks[8]&=~(1u<<2);PhysicsWorld falling;initialized(falling,pack,0);tick(falling,125);CHECK(falling.playerPosition().y<0);
 PhysicsWorld invalid;initialized(invalid,fixture(),0);MovementResult bad;bad.queuedForce={NAN,0,0};invalid.applyMovement(bad);std::string error;CHECK(!invalid.step(.008f,error));CHECK(finite(invalid.playerVelocity()));
 auto reference=runAtFps(20);for(unsigned fps:{30u,60u,120u}){auto result=runAtFps(fps);close(result.x,reference.x,.0001f);close(result.y,reference.y,.0001f);close(result.z,reference.z,.0001f);}
 std::puts("PASS: static contacts, capsule center/height, trigger and layer filtering, 0/2 workers, queued impulses/forces, CCD and render-independent fixed simulation");
}
void corruptPack(const char*path){
 FILE*f=std::fopen(path,"rb");CHECK(f);std::fseek(f,0,SEEK_END);long length=std::ftell(f);std::rewind(f);std::vector<uint8_t> bytes(length);CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());std::fclose(f);
 auto reject=[](const std::vector<uint8_t>&data){FILE*temp=std::tmpfile();CHECK(temp);CHECK(std::fwrite(data.data(),1,data.size(),temp)==data.size());std::rewind(temp);PhysicsPack pack;const char*error=nullptr;bool accepted=pack.read(temp,&error);std::fclose(temp);CHECK(!accepted);};
 auto bad=bytes;bad.pop_back();reject(bad);
 bad=bytes;reinterpret_cast<PhysicsHeader*>(bad.data())->colliderCount=UINT32_MAX;reject(bad);
 for(unsigned i=0;i<100;i++){bad=bytes;bad[sizeof(PhysicsHeader)+(i*104729u)%(bad.size()-sizeof(PhysicsHeader))]^=1;reject(bad);}
 auto refresh=[](std::vector<uint8_t>&data){reinterpret_cast<PhysicsHeader*>(data.data())->crc=crc32(data.data()+sizeof(PhysicsHeader),data.size()-sizeof(PhysicsHeader));};
 bad=bytes;reinterpret_cast<PlayerSpec*>(bad.data()+sizeof(PhysicsHeader))->mass=0;refresh(bad);reject(bad);
 bad=bytes;auto offset=sizeof(PhysicsHeader)+sizeof(PlayerSpec)+128;reinterpret_cast<ColliderRecord*>(bad.data()+offset)->layer=32;refresh(bad);reject(bad);
 bad=bytes;reinterpret_cast<ColliderRecord*>(bad.data()+offset)->world[0]=NAN;refresh(bad);reject(bad);
 bad=bytes;reinterpret_cast<PlayerSpec*>(bad.data()+sizeof(PhysicsHeader))->groundCollider=INT64_MAX;refresh(bad);reject(bad);
 std::puts("PASS: physics pack budgets, exact length, CRC mutations, player configuration, layer and finite transform validation");
}
void poseAndCast(){
 auto pack=fixture();PhysicsWorld world;initialized(world,pack,0);tick(world,250);std::string error;auto standing=world.playerPosition();
 CHECK(world.playerLayer()==2);CHECK(world.setPlayerLayer(15,error));CHECK(world.playerLayer()==15);CHECK(!world.setPlayerLayer(32,error));CHECK(world.playerLayer()==15);CHECK(world.setPlayerLayer(2,error));
 CHECK(world.setPlayerCapsule(1.25f,{0,-1.125f,0},{0,1.125f,0},error));close(world.playerHeight(),1.25f,0);close(world.playerPosition().y,standing.y-1.125f,.00001f);
 tick(world,100);close(world.playerPosition().y,.375f,.03f);
 std::vector<int64_t> contacts;CHECK(world.groundOverlaps(false,false,contacts,error));CHECK(std::find(contacts.begin(),contacts.end(),2)!=contacts.end());
 auto before=world.playerPosition();CHECK(!world.setPlayerCapsule(.5f,{},{},error));CHECK(!world.setPlayerCapsule(3.5f,{NAN,0,0},{},error));close(world.playerHeight(),1.25f,0);close(world.playerPosition().y,before.y,0);
 MovementResult force;force.useGravity=false;force.queuedForce={1000,0,0};world.applyMovement(force);tick(world,1);close(world.playerVelocity().x,.08f,.0001f); // mass remains 100 after resize
 CHECK(world.setPlayerCapsule(3.5f,{0,1.125f,0},{},error));close(world.playerHeight(),3.5f,0);close(world.playerPosition().y,standing.y,.03f);
 RayHit hit;CHECK(world.sphereCast({0,3,0},.5f,{0,-10,0},1u<<8,false,hit,error));CHECK(hit.hit&&hit.collider==2);close(hit.distance,2.5f,.002f);close(hit.point.y,0,.002f);CHECK(hit.normal.y>.999f);
 CHECK(world.sphereCast({0,3,0},.5f,{0,-10,0},1u<<7,false,hit,error));CHECK(!hit.hit);
 CHECK(!world.sphereCast({0,3,0},-1,{0,-10,0},~0u,false,hit,error));CHECK(!world.sphereCast({0,3,0},.5f,{},~0u,false,hit,error));
 // Start overlapping the floor, but retain a separate overhead obstruction.
 auto ceiling=pack.colliders[0];ceiling.id=6;ceiling.objectId=6;ceiling.center[1]=3.25f;ceiling.size[1]=.5f;pack.colliders.push_back(ceiling);
 PhysicsWorld ceilingWorld;initialized(ceilingWorld,pack,0);
 auto ray=ceilingWorld.ray({0,-.5f,0},{0,5,0},1u<<8,false);CHECK(ray.hit&&ray.collider==6);close(ray.distance,3.5f,.001f); // ignore origin inside floor
 CHECK(ceilingWorld.sphereCast({0,.25f,0},.5f,{0,3,0},1u<<8,false,hit,error));CHECK(hit.hit&&hit.collider==6);close(hit.distance,2.25f,.002f);CHECK(hit.normal.y<-.999f);
 CHECK(ceilingWorld.sphereCast({0,3,0},.1f,{0,-4,0},1u<<8,false,hit,error));CHECK(hit.hit&&hit.collider==2); // initial ceiling overlap excluded
 PhysicsWorld filtered;initialized(filtered,fixture(),0);tick(filtered,250);CHECK(!filtered.setLayerCollision(2,32,false,error));CHECK(filtered.setLayerCollision(2,8,false,error));tick(filtered,125);CHECK(filtered.playerPosition().y<0);
 initialized(filtered,fixture(),0);CHECK(filtered.setLayerCollision(2,8,false,error));CHECK(filtered.setLayerCollision(8,2,true,error));tick(filtered,250);close(filtered.playerPosition().y,1.5f,.03f);
 std::puts("PASS: capsule height/position/probe changes, preserved mass, layer updates, sphere sweep distance/normal/filtering and exclusion of initial overlaps without losing later hits");
}
void activation(){
 auto pack=fixture();auto wall=pack.colliders[0];wall.id=6;wall.objectId=6;wall.flags=Enabled;wall.center[0]=5;wall.center[1]=2.5f;wall.size[0]=1;wall.size[1]=5;wall.size[2]=5;pack.colliders.push_back(wall);PhysicsWorld world;initialized(world,pack,0);std::string error;
 CHECK(world.colliderMetadata(6)&&!(world.colliderMetadata(6)->flags&Active));CHECK(!world.staticColliderActive(6));CHECK(!world.ray({0,2,0},{10,0,0},1u<<8,false).hit);
 CHECK(world.setStaticColliderActive(6,true,error));CHECK(world.staticColliderActive(6));auto hit=world.ray({0,2,0},{10,0,0},1u<<8,false);CHECK(hit.hit&&hit.collider==6);close(hit.distance,4.5f,.001f);CHECK((world.colliderMetadata(6)->flags&3)==3);
 unsigned allocated=world.coverage().staticBodies;for(unsigned i=0;i<20;i++)CHECK(world.setStaticColliderActive(6,true,error));CHECK(world.coverage().staticBodies==allocated);
 CHECK(world.setStaticColliderActive(6,false,error));CHECK(!world.staticColliderActive(6)&&!(world.colliderMetadata(6)->flags&Active));CHECK(!world.ray({0,2,0},{10,0,0},1u<<8,false).hit);CHECK(world.setStaticColliderActive(6,true,error));CHECK(world.coverage().staticBodies==allocated);
 CHECK(!world.setStaticColliderActive(4,true,error));CHECK(!world.setStaticColliderActive(-1,true,error));CHECK(!world.colliderMetadata(-1));tick(world,250);CHECK(world.setStaticColliderActive(2,false,error));tick(world,125);CHECK(world.playerPosition().y<0);
 CHECK(world.setStaticColliderActive(6,false,error)); // safe destruction of cached bodies outside broadphase
 PhysicsWorld independent;{auto temporary=pack;initialized(independent,temporary,0);}CHECK(independent.setStaticColliderActive(6,true,error));CHECK(independent.staticColliderActive(6));CHECK(!(pack.colliders.back().flags&Active));
 std::puts("PASS: lazy authored static activation, query visibility, stable cached-body reuse, runtime contact metadata, batched support wakeup and independent source-data ownership; dynamic activation still excluded");
}
}
int main(int argc,char**argv){
 fixtures();poseAndCast();activation();if(argc<2)return 0;
 corruptPack(argv[1]);PhysicsPack pack;const char*error=nullptr;CHECK(pack.load(argv[1],&error));PhysicsWorld world;
 auto start=std::chrono::steady_clock::now();initialized(world,pack,2);auto coverage=world.coverage();
 CHECK(pack.colliders.size()==3602);CHECK(coverage.staticBodies==329 && coverage.sensors==22 && coverage.unsupported==0);
 for(unsigned i=0;i<1000;i++){tick(world,1);CHECK(finite(world.playerPosition()));}
 std::vector<int64_t> ground,slope;std::string queryError;
 CHECK(world.groundOverlaps(false,false,ground,queryError));CHECK(!ground.empty());CHECK(world.groundOverlaps(true,false,slope,queryError));CHECK(!slope.empty());
 auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();auto position=world.playerPosition();
 std::printf("PASS: actual level 1-2: %zu collider records retained; %u static bodies including %u sensors; %u authored dynamic colliders deferred; 1000 ticks, player=(%.3f %.3f %.3f), host init+simulation %.3fs (not Switch FPS or gameplay parity)\n",pack.colliders.size(),coverage.staticBodies,coverage.sensors,coverage.dynamicDeferred,position.x,position.y,position.z,elapsed);
 unsigned authoredStatic=0;for(const auto&c:pack.colliders)if(!(c.flags&AttachedBody)){authoredStatic++;if(!world.setStaticColliderActive(c.id,true,queryError)){std::fprintf(stderr,"activate %lld: %s\n",(long long)c.id,queryError.c_str());CHECK(false);}}
 CHECK(authoredStatic==1887);for(const auto&c:pack.colliders)if(!(c.flags&AttachedBody))CHECK(world.setStaticColliderActive(c.id,(c.flags&(Active|Enabled))==(Active|Enabled),queryError));
 unsigned active=0;for(const auto&c:pack.colliders)active+=world.staticColliderActive(c.id);CHECK(active==329);tick(world,1);CHECK(finite(world.playerPosition()));
 std::printf("PASS: all %u authored static collider definitions in actual level create successfully, including initially inactive rooms; original 329-body active state restored; scene-script/render bindings incomplete\n",authoredStatic);
}
