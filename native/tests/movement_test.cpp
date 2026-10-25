#include "movement_reference_support.h"
#include "movement_original.inc"
#include <random>
#include <cstdio>
#include <cstdlib>
using nx::Vec3;
using namespace nx::game;
namespace {
unsigned cases=0;
void equal(float a,float b,const char*field){
 if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>3e-4f+std::max(std::abs(a),std::abs(b))*1e-5f){std::fprintf(stderr,"case %u %s native=%.9g original=%.9g\n",cases,field,a,b);std::abort();}
}
void equal(Vec3 a,Vec3 b,const char*field){equal(a.x,b.x,field);equal(a.y,b.y,field);equal(a.z,b.z,field);}
void compare(const MovementInput&i){
 MovementState state;state.slideEnding=true;state.exploded=true;state.layer=15;
 state.wallJumps=2;state.rocketJumps=3;state.hammerJumps=4;state.rocketRides=5;
 state.targetVelocity={3,4,5};state.airDirection={5,6,7};
 reference::Body body;body.velocity=i.velocity;body.gravity=i.gravityDirection;body.mass=i.mass;
 reference::GroundCheck ground{i.grounded},slope{i.onSlope};
 reference::Ground surface;surface.speedMultiplier=i.surfaceSpeedMultiplier;surface.push=i.surfacePush;surface.pushForce=i.surfacePushWorld;
 reference::Movement original;original.rb=&body;original.gc=&ground;original.slopeCheck=&slope;original.groundProperties=i.hasSurface?&surface:nullptr;
 original.transform.forward=i.forward;original.transform.right=i.right;original.inputDir=i.inputDirection;original.pushForce=i.pushForce;
 original.walkSpeed=i.walkSpeed;original.airAcceleration=i.airAcceleration;original.friction=i.friction;original.windState=i.windState;
 original.hurtInvincibility=i.hurtInvincibility;original.levelOver=i.levelOver;original.jumping=i.jumping;original.slowMode=i.slowMode;original.fakeFallRequests=i.fakeFallRequests;
 original.slideEnding=state.slideEnding;original.exploded=state.exploded;original.gameObject.layer=state.layer;
 original.currentWallJumps=state.wallJumps;original.rocketJumps=state.rocketJumps;original.hammerJumps=state.hammerJumps;original.rocketRides=state.rocketRides;
 original.targetVel=state.targetVelocity;original.airDirection=state.airDirection;
 reference::dt=i.deltaTime;reference::fixedDt=i.fixedDeltaTime;
 auto result=move(i,state);reference::originalMove(&original);
 equal(result.velocity,body.velocity,"velocity");equal(result.queuedForce,body.force,"queuedForce");equal(result.queuedVelocityChange,body.delta,"queuedVelocityChange");
 equal(state.targetVelocity,original.targetVel,"targetVelocity");equal(state.airDirection,original.airDirection,"airDirection");
 assert(result.useGravity==body.useGravity && result.velocityWritten==body.written);
 assert(state.slideEnding==original.slideEnding && state.exploded==original.exploded && state.layer==original.gameObject.layer);
 assert(state.wallJumps==original.currentWallJumps && state.rocketJumps==original.rocketJumps && state.hammerJumps==original.hammerJumps && state.rocketRides==original.rocketRides);
 cases++;
}
}
int main(){
 MovementInput input;input.walkSpeed=750;input.airAcceleration=6000;input.mass=100;
 for(bool grounded:{false,true})for(bool slope:{false,true})for(bool jumping:{false,true})for(bool slow:{false,true})for(float friction:{-1.f,0.f,.1f,1.f,8.f})for(float wind:{-.251f,-.25f,-.249f,-.001f,0.f,.001f,1.f})for(float speed:{0.f,16.499f,16.5f,16.501f,100.f})for(Vec3 direction:{Vec3{},Vec3{1,0,0},Vec3{0,0,1},Vec3{.70710677f,0,.70710677f},Vec3{.000001f,0,.000001f}}){
  input.grounded=grounded;input.onSlope=slope;input.jumping=jumping;input.slowMode=slow;input.friction=friction;input.windState=wind;input.velocity={speed,-10,speed};input.inputDirection=direction;input.fakeFallRequests=1;compare(input);
 }
 std::mt19937 random(0x4e584d56);std::uniform_real_distribution<float> unit(-1,1);
 for(unsigned n=0;n<100000;n++){
  input.velocity={unit(random)*150,unit(random)*150,unit(random)*150};input.inputDirection=nx::normalized(Vec3{unit(random),0,unit(random)})*std::abs(unit(random));
  if(n%5==0)input.inputDirection={};
  float yaw=unit(random)*3.14159265f;input.forward={std::sin(yaw),0,std::cos(yaw)};input.right={std::cos(yaw),0,-std::sin(yaw)};
  input.gravityDirection=n%3?Vec3{0,-1,0}:nx::normalized(Vec3{unit(random),unit(random),unit(random)});
  input.pushForce={unit(random)*10,unit(random)*10,unit(random)*10};input.surfacePushWorld={unit(random)*10,unit(random)*10,unit(random)*10};
  input.grounded=random()%2;input.onSlope=random()%2;input.jumping=random()%2;input.slowMode=random()%2;input.hasSurface=random()%2;input.surfacePush=random()%2;
  input.hurtInvincibility=unit(random);input.levelOver=random()%2;input.fakeFallRequests=random()%3;input.friction=unit(random)*4;input.surfaceSpeedMultiplier=unit(random)*2;
  input.mass=1+std::abs(unit(random))*199;input.windState=unit(random);input.deltaTime=.004f+std::abs(unit(random))*.02f;input.fixedDeltaTime=.008f;
  compare(input);
 }
 // Behavioral acceptance examples, independent of the extracted oracle.
 input=MovementInput{};input.walkSpeed=750;input.airAcceleration=6000;input.mass=100;input.grounded=true;input.friction=4;input.inputDirection={0,0,1};
 MovementState state;auto moving=move(input,state);equal(moving.velocity,{0,0,16.5f},"authored walking speed");
 input.onSlope=true;input.inputDirection={};input.velocity={0,-5,0};auto resting=move(input,state);assert(!resting.useGravity);equal(resting.velocity,{},"idle slope");
 input.grounded=false;input.inputDirection={0,0,1};input.velocity={};auto airborne=move(input,state);equal(airborne.velocity,{},"AddForce must remain queued");assert(airborne.queuedVelocityChange.z>0);
 equal(rechargeStamina(0,1,2,false,false),70,"normal stamina");equal(rechargeStamina(0,1,1,false,false),105,"lenient stamina");equal(rechargeStamina(0,1,0,false,false),140,"harmless stamina");
 equal(rechargeStamina(290,1,2,false,false),300,"stamina cap");equal(rechargeStamina(40,1,0,true,false),40,"sliding stamina");equal(rechargeStamina(40,1,0,false,true),40,"slow mode stamina");
 std::printf("PASS: %u cases against original IL2CPP Move; ground/slope/friction, aerial steering/wind, external push, arbitrary gravity, queued force semantics and stamina cases\n",cases);
}
