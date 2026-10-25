#pragma once
#include "nx_scene.h"

namespace nx::game {
// Boundary for NewMovement.Move from TEST16. The physics layer supplies these
// observations; this module never invents ground contacts from visible meshes.
struct MovementInput {
 Vec3 velocity{},inputDirection{},pushForce{};
 Vec3 gravityDirection{0,-1,0},forward{0,0,1},right{1,0,0};
 Vec3 surfacePushWorld{}; // adapter resolves relative surface rotation first
 float deltaTime=.008f,fixedDeltaTime=.008f;
 float walkSpeed=0,airAcceleration=0,mass=0;
 float friction=1,surfaceSpeedMultiplier=1,windState=-1,hurtInvincibility=0;
 bool grounded=false,onSlope=false,jumping=false,slowMode=false,levelOver=false;
 bool hasSurface=false,surfacePush=false;
 int fakeFallRequests=0;
};
struct MovementState {
 Vec3 targetVelocity{},airDirection{};
 bool slideEnding=false,exploded=false;
 int layer=2,wallJumps=0,rocketJumps=0,hammerJumps=0,rocketRides=0;
};
struct MovementResult {
 Vec3 velocity{},queuedVelocityChange{},queuedForce{};
 bool useGravity=true,velocityWritten=false;
};
// Force and VelocityChange remain queued until the physics step. In Unity,
// AddForce does not immediately alter the velocity property read by scripts.
MovementResult move(const MovementInput&,MovementState&);
float rechargeStamina(float charge,float deltaTime,int difficulty,bool sliding,bool slowMode);
}
