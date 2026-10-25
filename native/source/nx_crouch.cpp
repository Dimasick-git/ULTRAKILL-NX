#include "nx_crouch.h"
namespace nx::game {
CrouchResult crouchPose(const CrouchInput&i,CrouchState&s){
 CrouchResult out;
 if(i.sliding){
  out.stopSlide=i.slideCanceled||(s.slowMode&&!s.crouching);out.updateSlide=true;s.standing=false;s.cameraTargetOffset={0,-.625f,0};return out;
 }
 if(i.hasSurface&&i.forceCrouch){
  out.setHeight=true;out.height=1.25f;s.crouching=true;
  if(s.standing){s.standing=false;out.positionDelta={0,-1.125f,0};out.setGroundOffset=true;out.groundOffset=i.up*1.125f;}
  s.cameraTargetOffset=i.up*-.625f;return out;
 }
 out.cleanupSlideEffects=true;
 if(i.capsuleHeight!=3.5f){
  if(i.overheadRay||i.overheadSphere){s.crouching=true;s.slowMode=true;return out;}
  s.crouching=false;s.slowMode=false;out.setHeight=true;out.height=3.5f;out.setGroundOffset=true;
  if(i.floorRay)out.positionDelta=i.up*1.125f;
  else{out.positionDelta=i.up*-.625f;s.cameraTargetOffset={};s.standing=true;}
  out.checkPortalCrossing=true;return out;
 }
 if(dot(s.cameraTargetOffset,s.cameraTargetOffset)>=9.99999944e-11f)s.cameraTargetOffset={};
 else s.standing=true;
 return out;
}
}
