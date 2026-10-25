#pragma once
#include "nx_movement.h"
#include <cstring>
#include <cassert>
#include <limits>
namespace reference {
using nx::Vec3;
struct Rotation{};
struct Transform {Vec3 forward{0,0,1},right{1,0,0};};
struct GameObject {int layer=2;};
struct Body {Vec3 velocity{},gravity{0,-1,0},force{},delta{};float mass=100;bool useGravity=true,written=false;};
struct GroundCheck {bool onGround=false;};
struct Ground {float speedMultiplier=1;bool push=false,pushDirectionRelative=false;Vec3 pushForce{};Transform transform;};
struct Movement {
 Body*rb;GroundCheck*gc,*slopeCheck;Ground*groundProperties;GameObject gameObject;Transform transform;
 float hurtInvincibility=0,friction=1,walkSpeed=750,windState=-1,airAcceleration=6000;
 Vec3 inputDir{},targetVel{},airDirection{},pushForce{};
 bool slideEnding=false,levelOver=false,exploded=false,jumping=false,slowMode=false;
 int currentWallJumps=0,rocketJumps=0,hammerJumps=0,rocketRides=0,fakeFallRequests=0;
};
inline float dt=.008f,fixedDt=.008f;
inline float mul(float a,float b){return a*b;}inline float add(float a,float b){return a+b;}inline float sub(float a,float b){return a-b;}
inline float Vector3_Dot(Vec3 a,Vec3 b,...){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float Vector3_get_magnitude(const Vec3*v,...){return std::sqrt(v->x*v->x+v->y*v->y+v->z*v->z);}
inline Vec3 Vector3_get_normalized(const Vec3*v,...){float n=Vector3_get_magnitude(v);if(n>1e-5f)return {v->x/n,v->y/n,v->z/n};return {};}
inline Vec3 Vector3_get_zero(...){return {};}
inline void Vector3__ctor(Vec3*v,float x,float y,float z,...){*v={x,y,z};}
inline Vec3 Vector3_op_Multiply(Vec3 v,float f,...){return {v.x*f,v.y*f,v.z*f};}
inline Vec3 Vector3_op_Multiply(float f,Vec3 v,...){return Vector3_op_Multiply(v,f);}
inline Vec3 Vector3_op_Addition(Vec3 a,Vec3 b,...){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 Vector3_op_Subtraction(Vec3 a,Vec3 b,...){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 Vector3_op_UnaryNegation(Vec3 a,...){return {-a.x,-a.y,-a.z};}
inline Vec3 Vector3_Project(Vec3 v,Vec3 n,...){float sq=Vector3_Dot(n,n);if(sq<std::numeric_limits<float>::denorm_min())return {};float d=Vector3_Dot(v,n);return {n.x*d/sq,n.y*d/sq,n.z*d/sq};}
inline Vec3 Vector3_ProjectOnPlane(Vec3 v,Vec3 n,...){return Vector3_op_Subtraction(v,Vector3_Project(v,n));}
inline Vec3 Vector3_ClampMagnitude(Vec3 v,float limit,...){float sq=Vector3_Dot(v,v);if(sq>limit*limit){float n=std::sqrt(sq);return {v.x/n*limit,v.y/n*limit,v.z/n*limit};}return v;}
inline float clamp(float t){return t<0?0:(t>1?1:t);}
inline float Mathf_Lerp(float a,float b,float t,...){return a+(b-a)*clamp(t);}
inline float Mathf_InverseLerp(float a,float b,float t,...){return a!=b?clamp((t-a)/(b-a)):0;}
inline float Mathf_Max(float a,float b,...){return a>b?a:b;}
inline Vec3 Vector3_Lerp(Vec3 a,Vec3 b,float t,...){t=clamp(t);return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
inline Vec3 Rigidbody_get_velocity(Body*b,...){return b->velocity;}
inline void Rigidbody_set_velocity(Body*b,Vec3 v,...){b->velocity=v;b->written=true;}
inline float Rigidbody_get_mass(Body*b,...){return b->mass;}
inline void Rigidbody_AddDefaultForce(Body*b,Vec3 v,...){b->force=Vector3_op_Addition(b->force,v);}
inline void Rigidbody_AddModeForce(Body*b,Vec3 v,int mode,...){assert(mode==2);b->delta=Vector3_op_Addition(b->delta,v);}
inline Vec3 PhysicsExtensions_GetGravityDirection(Body*b,...){return b->gravity;}
inline void PhysicsExtensions_SetGravityMode(Body*b,bool value,...){b->useGravity=value;}
inline float Time_get_deltaTime(...){return dt;}inline float Time_get_fixedDeltaTime(...){return fixedDt;}
inline bool GroundCheckGroup_get_onGround(GroundCheck*g,...){return g->onGround;}
inline bool Object_op_Implicit(Ground*g,...){return g!=nullptr;}
inline GameObject*Component_get_gameObject(Movement*m,...){return &m->gameObject;}
inline void GameObject_set_layer(GameObject*g,int layer,...){g->layer=layer;}
inline Transform*Component_get_transform(Movement*m,...){return &m->transform;}
inline Transform*Component_get_transform(Ground*g,...){return &g->transform;}
inline Vec3 Transform_get_forward(Transform*t,...){return t->forward;}
inline Vec3 Transform_get_right(Transform*t,...){return t->right;}
inline Rotation Transform_get_rotation(Transform*,...){return {};}
// Relative push is resolved at the production boundary; reference fixtures
// pass an already world-space push and leave pushDirectionRelative false.
inline Vec3 Quaternion_op_Multiply(Rotation,Vec3 v,...){return v;}
}
