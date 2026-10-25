#include "nx_revolver.h"
#include "revolver_reference_stubs.h"
#include "revolver_original.inc"
#include <cstdio>
#include <cstdlib>
using namespace nx::game;namespace ref=revolver_reference;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d, case %u: %s\n",__LINE__,caseNumber,#c);std::exit(1);}}while(false)
unsigned caseNumber=0;uint32_t seed=0xf29a10d3;uint32_t randomBits(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}float randomFloat(float lo,float hi){return lo+(hi-lo)*float(randomBits()&65535)/65535.f;}
void near(float a,float b){if(std::abs(a-b)>2e-5f*std::max({1.f,std::abs(a),std::abs(b)})){std::fprintf(stderr,"actual %g expected %g\n",a,b);CHECK(false);}}
struct Bridge:RevolverBridge {
 std::vector<int>events;float rev1=0,rev2=0;
 void shoot(RevolverState&,unsigned type)override{events.push_back(int(type));}
 void throwCoin(RevolverState&)override{events.push_back(3);}
 void delayedShot(unsigned type,float)override{events.push_back(type==1?4:5);}
 void delayedCoin(float)override{events.push_back(6);}
 void consumeCoinCharge(float value)override{rev1-=value;}
 void checkCoinCharges(RevolverState&s)override{events.push_back(7);s.coinCharge=s.variation==1?rev1:rev2;}
 void stopCameraShake()override{events.push_back(8);}
 void chargedSound()override{events.push_back(100);}
};
int main(){
 for(caseNumber=0;caseNumber<240000;caseNumber++){
  RevolverState native;uint32_t flags=randomBits();native.variation=caseNumber%3;native.alternate=flags&1;native.gunReady=flags&2;native.shootReady=flags&4;native.pierceReady=flags&8;native.charging=flags&16;native.twirlRecovery=flags&32;native.shootCharge=randomFloat(-10,110);native.pierceCharge=randomFloat(-10,110);native.shotCharge=caseNumber%5==0?100:caseNumber%5==1?0:caseNumber%5==2?25:randomFloat(0,100);native.coinCharge=caseNumber%7==0?100:caseNumber%7==1?300:randomFloat(0,400);native.cylinderSpin=randomFloat(-20,120);native.latestTwirlRotation=randomFloat(-180,180);native.delay=flags&64?.15f:0;native.chargeAudioVolume=randomFloat(0,1);native.chargeAudioPitch=randomFloat(0,1);native.lightEnabled=flags&128;native.lightRange=randomFloat(0,2);native.lightPresent=flags&256;native.chargeAudioPresent=native.variation!=1||bool(flags&512);
  RevolverFrame frame;frame.deltaTime=caseNumber%11==0?0:caseNumber%11==1?.008f:caseNumber%11==2?1:randomFloat(.0001f,.1f);frame.activated=flags&1024;frame.fire1=flags&2048;frame.fire2=flags&4096;frame.fire2Canceled=flags&8192;frame.fire2Performed=flags&16384;frame.cheatCombo=flags&32768;frame.inputLocked=flags&65536;frame.noCooldown=flags&131072;
  ref::Revolver original;original.gunVariation=int(native.variation);original.altVersion=native.alternate;original.gunReady=native.gunReady;original.shootReady=native.shootReady;original.pierceReady=native.pierceReady;original.chargingPierce=native.charging;original.twirlRecovery=native.twirlRecovery;original.shootCharge=native.shootCharge;original.pierceCharge=native.pierceCharge;original.pierceShotCharge=native.shotCharge;original.coinCharge=native.coinCharge;original.latestTwirlRotation=native.latestTwirlRotation;
  ref::dummy={};ref::globals={};ref::audio={};ref::light={};ref::cylinder={};ref::guns={};ref::identifier={};ref::charges={};ref::events.clear();ref::cylinder.spinSpeed=native.cylinderSpin;ref::guns.activated=frame.activated;ref::identifier.delay=native.delay;ref::audio.volume=native.chargeAudioVolume;ref::audio.pitch=native.chargeAudioPitch;ref::light.enabled=native.lightEnabled;ref::light.range=native.lightRange;ref::charges.rev1charge=randomFloat(0,400);ref::charges.rev2charge=randomFloat(0,300);
  original.inman=&ref::inputManager;original.wid=flags&262144?&ref::identifier:nullptr;if(!original.wid)native.delay=0;original.gc=&ref::guns;original.cylinder=&ref::cylinder;original.ceaud=native.chargeAudioPresent?&ref::audio:nullptr;original.celight=native.lightPresent?&ref::light:nullptr;original.wc=&ref::charges;original.screenAud=original.screenProps=original.chargedSound=original.batteryLow=original.batteryMid=original.batteryFull=original.wpos=original.cc=original.chargeEffect=original.screenMR=&ref::dummy;original.batteryCharges=&ref::textures;
  ref::deltaTime=frame.deltaTime;ref::cheatCombo=frame.cheatCombo;ref::inputLocked=frame.inputLocked;ref::noCooldown=frame.noCooldown;ref::fire1={frame.fire1,false,false};ref::fire2={frame.fire2,frame.fire2Canceled,frame.fire2Performed};Bridge bridge;bridge.rev1=ref::charges.rev1charge;bridge.rev2=ref::charges.rev2charge;
  ref::originalUpdate(&original);std::string error;CHECK(updateRevolver(native,frame,bridge,error));
  CHECK(native.gunReady==original.gunReady&&native.shootReady==original.shootReady&&native.pierceReady==original.pierceReady&&native.charging==original.chargingPierce&&native.twirlRecovery==original.twirlRecovery);near(native.shootCharge,original.shootCharge);near(native.pierceCharge,original.pierceCharge);near(native.shotCharge,original.pierceShotCharge);near(native.coinCharge,original.coinCharge);near(native.cylinderSpin,ref::cylinder.spinSpeed);near(native.latestTwirlRotation,original.latestTwirlRotation);near(native.chargeAudioVolume,ref::audio.volume);near(native.chargeAudioPitch,ref::audio.pitch);near(native.lightRange,ref::light.range);CHECK(native.lightEnabled==ref::light.enabled);near(bridge.rev1,ref::charges.rev1charge);CHECK(bridge.events==ref::events);
 }
 std::puts("PASS: 240000 original r52 Revolver.Update comparisons across 3 variations, alternate versions, cooldown thresholds, charge/release/input locks and delayed-call ordering; Shoot/ThrowCoin/CheckCoinCharges and engine effects are recorded boundaries, not full combat");
}
