#pragma once
#include "movement_reference_support.h"
#include <vector>
namespace jump_reference {
using Vector3=nx::Vec3;
using reference::mul;using reference::add;using reference::sub;
using reference::Vector3__ctor;using reference::Vector3_get_normalized;
using reference::Vector3_ProjectOnPlane;using reference::Vector3_op_Multiply;
using reference::Vector3_get_zero;using reference::Vector3_op_Addition;
using reference::Vector3_Dot;using reference::Vector3_ClampMagnitude;using reference::Vector3_op_UnaryNegation;
using reference::Vector3_get_magnitude;using reference::Mathf_Max;
struct TimeSince {float time=0;};
struct AudioClip {};
struct AudioSource {float volume=0,pitch=0;AudioClip*clip=nullptr;};
struct GameObject {int id=0;AudioSource audio;int layer=2;};
struct Rigidbody {Vector3 velocity{},force{};bool written=false;Vector3 gravity{0,-1,0};};
struct Transform {Vector3 up{0,1,0},forward{0,0,1},position{},right{1,0,0};};
struct CustomGroundProperties {bool canJump=true,silentJumpFail=false;float jumpForceMultiplier=1;bool canDash=true,silentDashFail=false,canSlide=true,push=false,pushDirectionRelative=false;float speedMultiplier=1;Vector3 pushForce{};Transform transform;};
struct GroundCheckGroup {float superChance=0,bounceChance=0,extraChance=0;bool heavyFall=false,onGround=false;};
struct AssistController {bool majorEnabled=false,infiniteStamina=false;};
struct PlayerAnimations {};
struct Func_2 {};
struct U3CU3Ec {};
struct DelegateCache {Func_2*U3CU3E9__161_0=nullptr;U3CU3Ec*U3CU3E9=nullptr;};
using Object=GameObject;
struct Quaternion {Vector3 direction;};
struct CameraController {int dodgeDirection=0;};
struct RumbleManager {};struct RumbleKey {};struct PendingVibration {};struct RumbleCache {RumbleKey*Dash=nullptr,*Slide=nullptr;};
struct Vector2 {float x=0,y=0;};struct InputActionState {Vector2 value;};struct PlayerInput {InputActionState*Move=nullptr;};struct InputManager {PlayerInput*source=nullptr;};struct HookArm {bool beingPulled=false;};struct WallCheckGroup {bool onWall=false;};
enum InvokeName {NotJumping,JumpReady};
struct NewMovement {
 Rigidbody*rb;GroundCheckGroup*gc;CustomGroundProperties*groundProperties=nullptr;AssistController*asscon;AudioSource*aud;AudioClip*jumpSound;
 GameObject*quakeJumpSound,*dashJumpSound,*staminaFailSound;Transform transform;
 bool modNoJump=false,jumping=false,falling=false,quakeJump=false,sliding=false,slowMode=false,boost=false,enemyStepping=false,jumpCooldown=false;
 float fallTime=0,jumpPower=0,ssjMaxFrames=4,windState=0,boostCharge=300,walkSpeed=750,slamForce=0;
 TimeSince lastJump;Vector3 inputDir{},dodgeDirection{};double jumpTimestamp=0,slideTimestamp=0;
 double notJumpingAt=0,jumpReadyAt=0;int stopSlideCalls=0,trySSJCalls=0;Vector3 ssjDirection{};float ssjGain=0;
 bool modNoDashSlide=false,dashedFromGround=false;float boostLeft=0,dashStorage=0,fallSpeed=0;
 GameObject*dodgeParticle=nullptr,*currentFallParticle=nullptr;CameraController*cc=nullptr;AudioClip*dodgeSound=nullptr;
 float hurtInvincibility=0,friction=1,preSlideSpeed=0,preSlideDelay=0;bool levelOver=false,exploded=false,slideEnding=false;
 int framesSinceSlide=0;Vector3 pushForce{},targetVel{};GameObject gameObject;WallCheckGroup*wcGroup=nullptr;
 GameObject*currentSlideParticle=nullptr,*slideScrape=nullptr,*slideStopSound=nullptr;float slideLength=0,longestSlide=0;Vector3 preDashSpeed{},velocityAfterSlide{};
};
inline double now=0;inline float dt=.008f;inline std::vector<int> effects;
inline DelegateCache cache;inline Func_2 delegate;inline PlayerAnimations playerAnimations;
inline RumbleCache rumbleFields;inline RumbleManager rumble;inline Vector3 particlePosition{},particleDirection{};
inline InputManager input;inline HookArm hook;
inline InputManager*inputManager(...){return &input;}inline PlayerInput*InputManager_get_InputSource(InputManager*m,...){return m->source;}
inline Vector2 readMove(InputActionState*a,...){return a->value;}
inline HookArm*hookArm(...){return &hook;}inline bool Object_op_Implicit(HookArm*h,...){return h!=nullptr;}
inline bool WallCheckGroup_OnWall(WallCheckGroup*w,...){return w->onWall;}
inline float Time_get_fixedDeltaTime(...){return reference::fixedDt;}
inline Vector3 PhysicsExtensions_GetGravityDirection(Rigidbody*b,...){return b->gravity;}
inline GameObject*Component_get_gameObject(NewMovement*m,...){return &m->gameObject;}
inline void GameObject_set_layer(GameObject*g,int layer,...){g->layer=layer;}
inline Transform*Component_get_transform(CustomGroundProperties*g,...){return &g->transform;}
inline Quaternion Transform_get_rotation(Transform*,...){return {};}
inline Vector3 Quaternion_op_Multiply(Quaternion,Vector3 v,...){return v;}
inline Vector3 Transform_get_right(Transform*t,...){return t->right;}
inline float Mathf_MoveTowards(float current,float target,float delta,...){if(std::abs(target-current)<=delta)return target;return current+(target>current?delta:-delta);}
inline float Vector3_Angle(Vector3 a,Vector3 b,...){float denom=std::sqrt(nx::dot(a,a)*nx::dot(b,b));if(denom<1e-15f)return 0;return std::acos(std::max(-1.f,std::min(1.f,nx::dot(a,b)/denom)))*57.29578f;}
inline void NewMovement_CreateSlideScrape(NewMovement*,bool,bool,...){effects.push_back(9);}
inline void NewMovement_DetachSlideScrape(NewMovement*,...){effects.push_back(10);}
inline void CameraController_ResetToDefaultPos(CameraController*,...){effects.push_back(12);}
inline void Physics_IgnoreLayerCollision(int a,int b,bool ignore,...){assert(a==2&&b==12&&!ignore);effects.push_back(13);}
inline void RumbleManager_StopVibration(RumbleManager*,RumbleKey*,...){effects.push_back(14);}
inline RumbleCache*rumbleCache(){return &rumbleFields;}inline RumbleManager*rumbleManager(...){return &rumble;}
inline PendingVibration*RumbleManager_SetVibration(RumbleManager*,RumbleKey*,...){effects.push_back(7);return nullptr;}
inline Quaternion Quaternion_LookRotation(Vector3 direction,...){return {direction};}
inline GameObject*instantiateAt(GameObject*g,Vector3 position,Quaternion q,...){effects.push_back(g->id);particlePosition=position;particleDirection=q.direction;return g;}
inline bool Object_op_Inequality(Object*a,Object*b,...){return a!=b;}
inline void Object_Destroy(GameObject*,...){effects.push_back(8);}
inline bool Vector3_op_Equality(Vector3 a,Vector3 b,...){Vector3 d=a-b;return nx::dot(d,d)<9.99999944e-11f;}
inline Vector3 Transform_get_forward(Transform*t,...){return t->forward;}
inline Vector3 Transform_get_position(Transform*t,...){return t->position;}
inline bool GroundCheckGroup_get_onGround(GroundCheckGroup*g,...){return g->onGround;}
inline bool GroundCheckGroup_get_heavyFall(GroundCheckGroup*g,...){return g->heavyFall;}
inline DelegateCache*delegateCache(){return &cache;}inline Func_2*newDelegate(){return &delegate;}
inline void Func_2__ctor(Func_2*,U3CU3Ec*,intptr_t,...){ }
inline void writeBarrier(void**,void*){ }
inline TimeSince TimeSince_op_Implicit(float elapsed,...){return {float(now)-elapsed};}
inline bool Object_op_Implicit(CustomGroundProperties*g,...){return g!=nullptr;}
inline Transform*Component_get_transform(NewMovement*m,...){return &m->transform;}
inline Vector3 Transform_get_up(Transform*t,...){return t->up;}
inline float Time_get_deltaTime(...){return dt;}
inline void AudioSource_set_clip(AudioSource*a,AudioClip*c,...){a->clip=c;}
inline void AudioSource_set_volume(AudioSource*a,float v,...){a->volume=v;}
inline void AudioSourceExtensions_SetPitch(AudioSource*a,float p,...){a->pitch=p;}
inline void AudioSourceExtensions_Play(AudioSource*,bool,...){effects.push_back(1);}
inline GameObject*instantiate(GameObject*g,...){effects.push_back(g->id);return g;}
inline AudioSource*audioComponent(GameObject*g,...){return &g->audio;}
inline PlayerAnimations*animations(...){return &playerAnimations;}
inline void PlayerAnimations_Jump(PlayerAnimations*,...){effects.push_back(5);}
inline float Random_Range(float a,float b,...){return (a+b)*.5f;}
inline void MonoBehaviour_CancelInvoke(NewMovement*,InvokeName,...){ }
inline void MonoBehaviour_Invoke(NewMovement*m,InvokeName name,float delay,...){(name==NotJumping?m->notJumpingAt:m->jumpReadyAt)=now+double(delay);}
inline Vector3 Rigidbody_get_velocity(Rigidbody*b,...){return b->velocity;}
inline void Rigidbody_set_velocity(Rigidbody*b,Vector3 v,...){b->velocity=v;b->written=true;}
inline void Rigidbody_AddForce(Rigidbody*b,Vector3 v,...){b->force=b->force+v;}
inline float GroundCheckGroup_get_superJumpChance(GroundCheckGroup*g,...){return g->superChance;}
inline float GroundCheckGroup_get_bounceChance(GroundCheckGroup*g,...){return g->bounceChance;}
inline float GroundCheckGroup_get_extraJumpChance(GroundCheckGroup*g,...){return g->extraChance;}
inline void GroundCheckGroup_set_bounceChance(GroundCheckGroup*g,float v,...){g->bounceChance=v;}
inline void GroundCheckGroup_set_heavyFall(GroundCheckGroup*g,bool v,...){g->heavyFall=v;}
// Recorded boundaries: intentionally do not reuse native StopSlide/TrySSJ
// here. Cases keep SSJ outside its active window so its numeric behavior is
// excluded, while verifying the original always issues the request.
inline void NewMovement_StopSlide(NewMovement*m,...){m->stopSlideCalls++;}
inline void NewMovement_TrySSJ(NewMovement*m,Vector3 direction,float gain,Func_2*,...){m->trySSJCalls++;m->ssjDirection=direction;m->ssjGain=gain;}
}
