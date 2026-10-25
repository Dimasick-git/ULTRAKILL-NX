#include "nx_joint.h"
#include <utility>
namespace nx::game {
uint32_t actorChecksum(const ActorPack&p){uint32_t crc=crc32(p.bodies.data(),p.bodies.size()*sizeof(ActorRecord));return crc32(p.colliders.data(),p.colliders.size()*sizeof(ActorColliderRef),crc);}
bool JointPack::load(const char*path,std::string&error){FILE*f=std::fopen(path,"rb");if(!f){error="cannot open joint pack";return false;}JointPack candidate;bool ok=candidate.read(f,error);std::fclose(f);if(ok)*this=std::move(candidate);return ok;}
bool JointPack::read(FILE*f,std::string&error){
 error="invalid joint pack header";if(std::fread(&header,sizeof header,1,f)!=1||std::memcmp(header.magic,"NXJOINT\0",8)||header.version!=1||header.count>65536||header.reserved[0]||header.reserved[1]||header.reserved[2])return false;
 uint64_t bytes=uint64_t(header.count)*sizeof(JointRecord);long start=std::ftell(f);if(start<0||std::fseek(f,0,SEEK_END)||std::ftell(f)<start||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;joints.resize(header.count);if(std::fread(joints.data(),sizeof(JointRecord),joints.size(),f)!=joints.size())return false;if(crc32(joints.data(),joints.size()*sizeof(JointRecord))!=header.crc){error="joint checksum mismatch";return false;}return validate(error);
}
bool JointPack::validate(std::string&error)const{
 error="invalid joint configuration";if(header.version!=1||std::memcmp(header.magic,"NXJOINT\0",8)||header.count!=joints.size()||joints.size()>65536||header.reserved[0]||header.reserved[1]||header.reserved[2])return false;std::unordered_set<int64_t> ids;
 for(const auto&j:joints){
  if(!j.id||!j.body||!j.objectId||j.body==j.connected||!ids.insert(j.id).second||j.kind<CharacterJoint||j.kind>ConfigurableJoint||j.flags&~63u||j.projection>1||j.rotationDriveMode>1)return false;
  for(auto v:{j.anchor,j.connectedAnchor,j.axis,j.secondary,j.targetPosition,j.targetVelocity,j.targetAngularVelocity})if(!finite(v))return false;
  float axisLength=dot(j.axis,j.axis);if(!std::isfinite(axisLength)||axisLength<1e-12f)return false;
  for(float v:{j.linearLimit,j.twistLow,j.twistHigh,j.swing1,j.swing2,j.projectionDistance,j.projectionAngle,j.massScale,j.connectedMassScale})if(!std::isfinite(v))return false;
  if(j.linearLimit<0||j.twistLow<-180||j.twistHigh>180||j.twistLow>j.twistHigh||j.swing1<0||j.swing1>180||j.swing2<0||j.swing2>180||j.projectionDistance<0||j.projectionAngle<0||j.projectionAngle>180||j.massScale<=0||j.connectedMassScale<=0)return false;
  for(float v:{j.breakForce,j.breakTorque})if(std::isnan(v)||v<0)return false;
  for(auto v:j.springs)if(!std::isfinite(v)||v<0)return false;for(auto v:j.motion)if(v>2)return false;
  float length=0;for(float v:j.targetRotation){if(!std::isfinite(v))return false;length+=v*v;}if(std::abs(length-1)>.002f)return false;
  for(const auto&drive:j.drives){for(auto v:drive)if(!std::isfinite(v)||v<0)return false;if(drive[3]!=0&&drive[3]!=1)return false;}
  for(const auto&limit:j.limitDetails)if(!std::isfinite(limit[0])||!std::isfinite(limit[1])||limit[0]<0||limit[0]>1||limit[1]<0)return false;
  for(auto v:j.reserved)if(v)return false;
 }error.clear();return true;
}
bool JointPack::bind(const ActorPack&a,const GraphPack&g,std::string&error)const{
 if(!validate(error)||!a.validate(error)||!g.validate(error))return false;if(header.actorCrc!=actorChecksum(a)||header.graphCrc!=g.checksum()||a.header.graphCrc!=g.checksum()){error="joint actor/hierarchy identity mismatch";return false;}
 std::unordered_map<int64_t,const ActorRecord*> actors;std::unordered_map<int64_t,const GraphComponent*> components;std::unordered_set<int64_t> owners;for(const auto&b:a.bodies)actors.emplace(b.id,&b);for(const auto&c:g.components)components.emplace(c.id,&c);
 size_t expected=0;for(const auto&c:g.components){auto type=g.text(c.typeOffset,c.typeLength);expected+=type=="CharacterJoint"||type=="FixedJoint"||type=="ConfigurableJoint";}if(expected!=joints.size()){error="joint pack omits authored joint components";return false;}
 const char*types[]={"","CharacterJoint","FixedJoint","ConfigurableJoint"};
 for(const auto&j:joints){auto owner=actors.find(j.body),connected=actors.find(j.connected);auto c=components.find(j.id);if(owner==actors.end()||(j.connected&&connected==actors.end())||owner->second->objectId!=j.objectId||!(owner->second->flags&ActorHasJoint)||c==components.end()||g.nodes[c->second->node].objectId!=j.objectId||g.text(c->second->typeOffset,c->second->typeLength)!=types[j.kind]){error="joint component/body ownership mismatch";return false;}owners.insert(j.body);}
 for(const auto&b:a.bodies)if(bool(b.flags&ActorHasJoint)!=bool(owners.count(b.id))){error="joint/body dependency mismatch";return false;}error.clear();return true;
}
bool jointAxes(const JointRecord&j,Vec3&axis,Vec3&secondary,std::string&error){
 if(!finite(j.axis)||!finite(j.secondary)||dot(j.axis,j.axis)<1e-12f){error="invalid joint twist axis";return false;}axis=normalized(j.axis);secondary=j.secondary-axis*dot(j.secondary,axis);
 if(dot(secondary,secondary)<1e-12f){
  secondary=std::abs(axis.z)>.7071067811865475f?Vec3{0,-axis.z,axis.y}:Vec3{-axis.y,axis.x,0};
 }secondary=normalized(secondary);error.clear();return true;
}
bool jointAdapterSupported(const JointRecord&j,std::string&error){
 Vec3 axis,secondary;if(!jointAxes(j,axis,secondary,error))return false;
 if(j.massScale!=1||j.connectedMassScale!=1){error="per-joint mass scaling adapter pending";return false;}
 for(auto spring:j.springs)if(spring!=0){error="soft joint limits adapter pending";return false;}
 for(const auto&limit:j.limitDetails)if(limit[0]!=0||limit[1]!=0){error="joint limit bounce/contact-distance adapter pending";return false;}
 for(const auto&drive:j.drives)if(drive[0]!=0||drive[1]!=0){error="joint motor/drive adapter pending";return false;}
 if(j.flags&(JointWorldSpace|JointSwap)){error="joint world-drive/swap adapter pending";return false;}
 if(j.kind==ConfigurableJoint&&j.linearLimit>0)for(unsigned i=0;i<3;i++)if(j.motion[i]==1){error="spherical configurable linear limit adapter pending";return false;}
 if(j.projection&&j.projectionAngle<180){error="angular joint projection adapter pending";return false;}
 error.clear();return true;
}
}
