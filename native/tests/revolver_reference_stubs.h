#pragma once
#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>
namespace revolver_reference {
using il2cpp_array_size_t=unsigned;
struct Vector3 {float x=0,y=0,z=0;};struct Color {float r=0,g=0,b=0,a=1;};
struct Engine;struct Texture2DU5BU5D {Engine*GetAt(unsigned);};struct ColorU5BU5D {Color GetAt(unsigned){return {};}};
struct Engine {bool enabled=false,activated=false;float volume=0,pitch=0,range=0,spinSpeed=0,delay=0,intensityMultiplier=0,rev1charge=0,rev2charge=0;Vector3 currentDefault;ColorU5BU5D*variationColors=nullptr;Engine*RevolverCharge=nullptr;};
using Object=Engine;using AudioSource=Engine;using AudioClip=Engine;using Texture2D=Engine;using CameraController=Engine;using ColorBlindSettings=Engine;using GameObject=Engine;using GunControl=Engine;using Light=Engine;using MaterialPropertyBlock=Engine;using MeshRenderer=Engine;using PendingVibration=Engine;using RevolverCylinder=Engine;using RumbleKey=Engine;using RumbleManager=Engine;using Transform=Engine;using WeaponCharges=Engine;using WeaponIdentifier=Engine;using WeaponPos=Engine;using Component=Engine;using Behaviour=Engine;using Renderer=Engine;using Texture=Engine;using RumbleProperties_StaticFields=Engine;
struct InputActionState {bool pressed=false,canceled=false,performed=false;};struct PlayerInput {InputActionState*Fire1=nullptr,*Fire2=nullptr;};struct InputManager{};struct GameStateManager{};
struct Revolver {
 int gunVariation=0;bool altVersion=false,gunReady=false,shootReady=false,pierceReady=false,chargingPierce=false,twirlRecovery=false;
 float shootCharge=0,pierceCharge=0,pierceShotCharge=0,coinCharge=0,latestTwirlRotation=0;
 InputManager*inman=nullptr;Engine*wid=nullptr,*gc=nullptr,*cylinder=nullptr,*screenAud=nullptr,*chargedSound=nullptr,*batteryLow=nullptr,*batteryMid=nullptr,*batteryFull=nullptr,*screenProps=nullptr,*wpos=nullptr,*cc=nullptr,*ceaud=nullptr,*celight=nullptr,*chargeEffect=nullptr,*screenMR=nullptr,*wc=nullptr;Texture2DU5BU5D*batteryCharges=nullptr;
};
inline Engine dummy,globals,audio,light,cylinder,guns,identifier,charges;inline Texture2DU5BU5D textures;inline InputManager inputManager;inline InputActionState fire1,fire2;inline PlayerInput input{&fire1,&fire2};inline GameStateManager gameState;inline float deltaTime=0;inline bool cheatCombo=false,inputLocked=false,noCooldown=false;inline std::vector<int>events;
inline Engine*Texture2DU5BU5D::GetAt(unsigned){return &dummy;}
enum {DelayedCoin=0,DelayedNormal=1,DelayedSuper=2,MainTexture=3,TintColor=4};
#define NullCheck(x) ((void)(x))
#define il2cpp_codegen_static_fields_for(x) (&globals)
template<class A,class B>auto add(A a,B b){return a+b;}template<class A,class B>auto sub(A a,B b){return a-b;}template<class A,class B>auto mul(A a,B b){return a*b;}
inline float Time_get_deltaTime(void*){return deltaTime;}inline bool NoWeaponCooldown_get_NoCooldown(void*){return noCooldown;}
inline bool Object_op_Implicit(Engine*p,void*){return p!=nullptr;}
inline PlayerInput*InputManager_get_InputSource(InputManager*,void*){return &input;}
inline bool InputManager_PerformingCheatMenuCombo(InputManager*,void*){return cheatCombo;}
inline bool InputActionState_get_IsPressed(InputActionState*p,void*){return p->pressed;}
inline bool InputActionState_get_WasCanceledThisFrame(InputActionState*p,void*){return p->canceled;}
inline bool InputActionState_get_WasPerformedThisFrame(InputActionState*p,void*){return p->performed;}
inline GameStateManager*GameStateManager_get_Instance(void*){return &gameState;}inline bool GameStateManager_get_PlayerInputLocked(GameStateManager*,void*){return inputLocked;}
inline Engine*colorSettings(void*){return nullptr;}inline Engine*rumbleManager(void*){return &dummy;}
inline Color Color_get_red(void*){return {1,0,0,1};}inline Color Color_get_yellow(void*){return {1,1,0,1};}
template<class...A>void MaterialPropertyBlock_SetTexture(A...){ }template<class...A>void MaterialPropertyBlock_SetColor(A...){ }template<class...A>void Renderer_SetPropertyBlock(A...){ }
template<class...A>void AudioSource_set_clip(A...){ }template<class...A>void AudioSource_set_loop(A...){ }
inline void AudioSource_set_volume(Engine*p,float value,void*){p->volume=value;}inline float AudioSource_get_volume(Engine*p,void*){return p->volume;}
inline void AudioSourceExtensions_SetPitch(Engine*p,float value,void*){p->pitch=value;}
template<class...A>void AudioSourceExtensions_Play(A...){events.push_back(100);}
inline bool Behaviour_get_enabled(Engine*p,void*){return p->enabled;}inline void Behaviour_set_enabled(Engine*p,bool value,void*){p->enabled=value;}
inline void Light_set_range(Engine*p,float value,void*){p->range=value;}
inline float Mathf_MoveTowards(float current,float target,float delta,void*){return std::abs(target-current)<=delta?target:current+std::copysign(delta,target-current);}
template<class...A>Engine*Component_get_transform(A...){return &dummy;}template<class...A>Engine*GameObject_get_transform(A...){return &dummy;}template<class...A>Engine*Component_get_gameObject(A...){return &dummy;}
template<class...A>void Transform_set_localPosition(A...){ }template<class...A>void Transform_set_localScale(A...){ }
inline void Vector3__ctor(Vector3*p,float x,float y,float z,void*){*p={x,y,z};}inline Vector3 Vector3_get_one(void*){return {1,1,1};}inline Vector3 Vector3_op_Multiply(Vector3 a,float b,void*){return {a.x*b,a.y*b,a.z*b};}
inline float Random_Range(float a,float b,void*){return (a+b)*.5f;}template<class...A>Engine*RumbleManager_SetVibrationTracked(A...){return &dummy;}
inline void Revolver_Shoot(Revolver*,int type,void*){events.push_back(type);}
inline void Revolver_ThrowCoin(Revolver*,void*){events.push_back(3);}
inline void Revolver_CheckCoinCharges(Revolver*s,void*){events.push_back(7);s->coinCharge=s->gunVariation==1?s->wc->rev1charge:s->wc->rev2charge;}
inline void CameraController_StopShake(Engine*,void*){events.push_back(8);}
inline void MonoBehaviour_Invoke(Revolver*,int what,float,void*){events.push_back(what==DelayedNormal?4:what==DelayedSuper?5:6);}
}
