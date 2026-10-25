#pragma once
#include "nx_scene.h"
namespace nx::game {
struct CrouchState {bool crouching=false,standing=true,slowMode=false;Vec3 cameraTargetOffset{};};
struct CrouchInput {
 bool sliding=false,slideCanceled=false,hasSurface=false,forceCrouch=false;
 bool overheadRay=false,overheadSphere=false,floorRay=false;
 float capsuleHeight=3.5f;Vec3 up{0,1,0};
};
struct CrouchResult {
 bool stopSlide=false,updateSlide=false,cleanupSlideEffects=false,setHeight=false,setGroundOffset=false,checkPortalCrossing=false;
 float height=0;Vec3 positionDelta{},groundOffset{};
};
// HandleSlideState posture decisions. Queries, portal travel, camera smoothing
// and slide effects are supplied by their engine adapters. This preserves the
// original distinction between world-up and player-up expressions.
CrouchResult crouchPose(const CrouchInput&,CrouchState&);
}
