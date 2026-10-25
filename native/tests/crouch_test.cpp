#include "nx_crouch.h"
#include <cstdio>
#include <cstdlib>
using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
int main(){
 CrouchInput i;CrouchState s;i.sliding=true;i.slideCanceled=true;auto r=crouchPose(i,s);CHECK(r.stopSlide&&r.updateSlide&&!r.setHeight&&!s.standing&&s.cameraTargetOffset.y==-.625f);
 i={};i.hasSurface=true;i.forceCrouch=true;s={};r=crouchPose(i,s);CHECK(r.setHeight&&r.height==1.25f&&r.positionDelta.y==-1.125f&&r.groundOffset.y==1.125f&&s.crouching&&!s.standing);r=crouchPose(i,s);CHECK(r.positionDelta.y==0&&!r.setGroundOffset);
 i={};i.capsuleHeight=1.25f;i.overheadRay=true;r=crouchPose(i,s);CHECK(s.crouching&&s.slowMode&&!r.setHeight);i.overheadRay=false;i.overheadSphere=true;r=crouchPose(i,s);CHECK(s.crouching&&!r.setHeight);
 i.overheadSphere=false;i.floorRay=true;r=crouchPose(i,s);CHECK(!s.crouching&&!s.slowMode&&r.setHeight&&r.height==3.5f&&r.setGroundOffset&&r.groundOffset.y==0&&r.positionDelta.y==1.125f&&r.checkPortalCrossing);
 i.floorRay=false;r=crouchPose(i,s);CHECK(r.positionDelta.y==-.625f&&s.standing&&s.cameraTargetOffset.y==0);
 i={};i.up={1,0,0};i.hasSurface=true;i.forceCrouch=true;s={};r=crouchPose(i,s);CHECK(r.positionDelta.y==-1.125f&&r.positionDelta.x==0&&s.cameraTargetOffset.x==-.625f&&r.groundOffset.x==1.125f);
 i={};s={};s.standing=false;s.cameraTargetOffset={0,-.625f,0};r=crouchPose(i,s);CHECK(!s.standing&&s.cameraTargetOffset.y==0);r=crouchPose(i,s);CHECK(s.standing);
 std::puts("PASS: crouch/stand posture decisions, blocked headroom, capsule/probe commands, world-up/player-up distinctions and delayed standing state; original-method differential check and full portal/camera/effect integration pending");
}
