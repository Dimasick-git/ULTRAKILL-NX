#pragma once
#include <string>
#include <cstdint>
namespace nx::game {
// Original Revolver.Update state, independent of render/physics ticks. This
// calculation layer does not instantiate beams, animate models or play audio.
struct RevolverState {
 uint32_t variation=0;bool alternate=false,gunReady=false,shootReady=true,pierceReady=true,charging=false,twirlRecovery=false;
 float shootCharge=0,pierceCharge=0,shotCharge=0,coinCharge=400,cylinderSpin=0,latestTwirlRotation=0;
 float delay=0;bool chargeAudioPresent=true,lightPresent=true;
 float chargeAudioVolume=0,chargeAudioPitch=0,lightRange=0;bool lightEnabled=false;
};
struct RevolverFrame {
 float deltaTime=0;
 bool activated=true,fire1=false,fire2=false,fire2Canceled=false,fire2Performed=false,cheatCombo=false,inputLocked=false,noCooldown=false;
};
// Actual source callees remain explicit boundaries. ReadyGun/ReadyToShoot are
// animation callbacks; the caller must never synthesize them from a cooldown.
class RevolverBridge {
public:
 virtual ~RevolverBridge()=default;
 virtual void shoot(RevolverState&,unsigned shotType)=0;
 virtual void throwCoin(RevolverState&)=0;
 virtual void delayedShot(unsigned shotType,float seconds)=0;
 virtual void delayedCoin(float seconds)=0;
 virtual void consumeCoinCharge(float amount)=0;
 virtual void checkCoinCharges(RevolverState&)=0;
 virtual void stopCameraShake()=0;
 virtual void chargedSound()=0;
};
bool updateRevolver(RevolverState&,const RevolverFrame&,RevolverBridge&,std::string&error);
inline void revolverReadyGun(RevolverState&s){s.gunReady=true;}
inline void revolverReadyToShoot(RevolverState&s){s.shootReady=true;}
}
