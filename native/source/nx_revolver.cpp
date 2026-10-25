#include "nx_revolver.h"
#include <algorithm>
#include <cmath>
namespace nx::game {
bool updateRevolver(RevolverState&s,const RevolverFrame&f,RevolverBridge&bridge,std::string&error){
 if(s.variation>2||!std::isfinite(f.deltaTime)||f.deltaTime<0||!std::isfinite(s.delay)||s.delay<0){error="invalid revolver configuration/frame";return false;}
 for(float value:{s.shootCharge,s.pierceCharge,s.shotCharge,s.coinCharge,s.cylinderSpin,s.latestTwirlRotation,s.chargeAudioVolume,s.chargeAudioPitch,s.lightRange})if(!std::isfinite(value)){error="nonfinite revolver state";return false;}
 if(!s.shootReady){if(s.shootCharge+200.f*f.deltaTime<100.f)s.shootCharge+=200.f*f.deltaTime;else{s.shootCharge=100;s.shootReady=true;}}
 if(!s.pierceReady){
  if(s.variation==0){
   if(f.noCooldown)s.pierceCharge=100;
   float multiplier=s.alternate?.5f:1.f;
   // Preserve the original threshold: alternate scales the increment only.
   if(s.pierceCharge+40.f*f.deltaTime<100.f)s.pierceCharge+=40.f*f.deltaTime*multiplier;
   else{s.pierceCharge=100;s.pierceReady=true;bridge.chargedSound();}
   if(s.cylinderSpin>0)s.cylinderSpin=std::max(0.f,s.cylinderSpin-f.deltaTime*50.f);
  }else if(s.pierceCharge+480.f*f.deltaTime<100.f)s.pierceCharge+=480.f*f.deltaTime;
  else{s.pierceCharge=100;s.pierceReady=true;}
 }else if(s.variation==0)s.cylinderSpin=s.shotCharge!=0?s.shotCharge:0;
 if(f.activated){
  if(s.variation!=1&&s.gunReady){
   float rate=s.variation==0?175.f:s.alternate?750.f:75.f;
   if((f.fire2Canceled||(!f.cheatCombo&&!f.inputLocked&&f.fire1))&&s.shootReady&&(s.variation==0?s.shotCharge==100.f:s.shotCharge>=25.f)){
    if(s.delay==0)bridge.shoot(s,2);else{s.shootReady=false;s.shootCharge=0;bridge.delayedShot(2,s.delay);}
   }else if(!f.cheatCombo&&f.fire1&&s.shootReady&&!s.charging){
    if(s.delay==0)bridge.shoot(s,1);else{s.shootReady=false;s.shootCharge=0;bridge.delayedShot(1,s.delay);}
   }else if(f.fire2&&(s.variation==2||s.shootReady)&&(s.variation==0?s.pierceReady:s.coinCharge>=float(s.alternate?300:100))){
    if(!s.charging&&!s.twirlRecovery)s.latestTwirlRotation=0;s.charging=true;
    if(s.shotCharge+rate*f.deltaTime<100.f)s.shotCharge+=rate*f.deltaTime;else s.shotCharge=100;
   }else{
    if(s.charging)s.twirlRecovery=true;s.charging=false;
    if(s.shotCharge-rate*f.deltaTime>0)s.shotCharge-=rate*f.deltaTime;else s.shotCharge=0;
   }
  }else if(s.variation==1){
   if(f.fire2Performed&&s.pierceReady&&s.coinCharge>=100.f){
    bridge.stopCameraShake();if(s.delay==0)bridge.consumeCoinCharge(100);
    if(s.delay==0)bridge.throwCoin(s);else{bridge.delayedCoin(s.delay);s.pierceReady=false;s.pierceCharge=0;}
   }else if(s.gunReady&&!f.cheatCombo&&f.fire1&&s.shootReady){
    if(s.delay==0)bridge.shoot(s,1);else{s.shootReady=false;s.shootCharge=0;bridge.delayedShot(1,s.delay);}
    if(s.chargeAudioPresent&&s.chargeAudioVolume!=0)s.chargeAudioVolume=0;
   }
  }
 }
 if(s.lightPresent){if(s.shotCharge==0&&s.lightEnabled)s.lightEnabled=false;else if(s.shotCharge!=0){s.lightEnabled=true;s.lightRange=s.shotCharge*.01f;}}
 if(s.variation!=1){if(s.variation==0)s.chargeAudioPitch=s.shotCharge*.005f;s.chargeAudioVolume=.25f+s.shotCharge*.005f;}
 if(s.variation!=0)bridge.checkCoinCharges(s);
 error.clear();return true;
}
}
