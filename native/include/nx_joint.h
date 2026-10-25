#pragma once
#include "nx_actor.h"
namespace nx::game {
enum JointKind:uint32_t {CharacterJoint=1,FixedJoint=2,ConfigurableJoint=3};
enum JointFlags:uint32_t {JointEnabled=1,JointAutoAnchor=2,JointCollide=4,JointPreprocess=8,JointWorldSpace=16,JointSwap=32};
struct JointHeader {char magic[8];uint32_t version,count,actorCrc,graphCrc,crc,reserved[3];};
struct JointRecord {
 int64_t id,body,connected,objectId;uint32_t kind,flags,projection,rotationDriveMode;
 Vec3 anchor,connectedAnchor,axis,secondary;
 float linearLimit,twistLow,twistHigh,swing1,swing2;
 float projectionDistance,projectionAngle,breakForce,breakTorque,massScale,connectedMassScale;
 float springs[8];uint32_t motion[6];Vec3 targetPosition,targetVelocity,targetAngularVelocity;float targetRotation[4];
 float drives[6][4],limitDetails[5][2];uint32_t reserved[32];
};
static_assert(sizeof(JointHeader)==40&&sizeof(JointRecord)==512,"joint pack ABI");
uint32_t actorChecksum(const ActorPack&);
struct JointPack {
 JointHeader header{};std::vector<JointRecord> joints;
 bool load(const char*,std::string&);
 bool read(FILE*,std::string&);
 bool validate(std::string&)const;
 bool bind(const ActorPack&,const GraphPack&,std::string&)const;
};
// Orthogonal rigid-local directions. Degenerate secondary uses the Unity
// OrthoNormalize basis, verified against the local 2022.3 physics reference.
bool jointAxes(const JointRecord&,Vec3&axis,Vec3&secondary,std::string&);
bool jointAdapterSupported(const JointRecord&,std::string&);
struct NativeJointState {bool enabled=false,attached=false,broken=false;float anchorError=0;Vec3 ownerAnchor{},connectedAnchor{};};
}
