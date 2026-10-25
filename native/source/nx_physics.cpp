#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/SimShapeFilter.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include "nx_physics.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <cfloat>

namespace nx::game {
namespace {
JPH::Vec3 j(Vec3 v){return {v.x,v.y,v.z};}
Vec3 n(JPH::Vec3Arg v){return {v.GetX(),v.GetY(),v.GetZ()};}
std::mutex registryMutex;unsigned registryUsers=0;
struct Lifetime {
 Lifetime(){std::lock_guard<std::mutex> lock(registryMutex);if(!registryUsers++){JPH::RegisterDefaultAllocator();JPH::Factory::sInstance=new JPH::Factory;JPH::RegisterTypes();}}
 ~Lifetime(){std::lock_guard<std::mutex> lock(registryMutex);if(!--registryUsers){JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}}
};
struct BroadLayers final:JPH::BroadPhaseLayerInterface {
 JPH::uint GetNumBroadPhaseLayers()const override{return 2;}
 JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer)const override{return JPH::BroadPhaseLayer((layer&32)?1:0);}
};
struct BroadFilter final:JPH::ObjectVsBroadPhaseLayerFilter {
 bool ShouldCollide(JPH::ObjectLayer layer,JPH::BroadPhaseLayer broad)const override{return (layer&32)||broad.GetValue()==1;}
};
struct PairFilter final:JPH::ObjectLayerPairFilter {
 std::array<uint32_t,32> masks{};
 struct Actors {int64_t a,b;bool operator==(const Actors&other)const{return a==other.a&&b==other.b;}};
 struct Hash {size_t operator()(const Actors&key)const{return std::hash<int64_t>{}(key.a)^(std::hash<int64_t>{}(key.b)+0x9e3779b97f4a7c15ULL+(uint64_t(key.a)<<6)+(uint64_t(key.a)>>2));}};
 std::unordered_map<Actors,uint32_t,Hash> ignored;
 static Actors actorPair(int64_t a,int64_t b){return a<b?Actors{a,b}:Actors{b,a};}
 bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b)const override{return ((a|b)&32) && (((a|b)&128)||(masks[a&31]&(1u<<(b&31))));}
};
struct QueryLayers final:JPH::ObjectLayerFilter {
 uint32_t mask;bool triggers;
 QueryLayers(uint32_t m,bool t):mask(m),triggers(t){}
 bool ShouldCollide(JPH::ObjectLayer layer)const override{return (triggers||!(layer&64)) && ((layer&128)||(mask&(1u<<(layer&31))));}
};
struct BodyMeta {int64_t collider;uint32_t frictionMode,bounceMode;const ColliderRecord*record=nullptr;int64_t actor=0;};
const BodyMeta*leafMeta(const JPH::Body&body,const JPH::SubShapeID&id){auto data=body.GetShape()->GetSubShapeUserData(id);return reinterpret_cast<const BodyMeta*>(data?data:body.GetUserData());}
struct QueryShapes final:JPH::ShapeFilter {
 uint32_t mask;bool triggers;
 QueryShapes(uint32_t m,bool t):mask(m),triggers(t){}
 bool accepts(const JPH::Shape*shape)const{auto*m=reinterpret_cast<const BodyMeta*>(shape->GetUserData());if(!m||!m->record)return true;const auto&c=*m->record;return (c.flags&(Active|Enabled))==(Active|Enabled)&&(mask&(1u<<c.layer))&&(triggers||!(c.flags&Trigger));}
 bool ShouldCollide(const JPH::Shape*shape,const JPH::SubShapeID&)const override{return accepts(shape);}
 bool ShouldCollide(const JPH::Shape*,const JPH::SubShapeID&,const JPH::Shape*shape,const JPH::SubShapeID&)const override{return accepts(shape);}
};
struct SimulationShapes final:JPH::SimShapeFilter {
 const PairFilter&pairs;explicit SimulationShapes(const PairFilter&p):pairs(p){}
 bool ShouldCollide(const JPH::Body&a,const JPH::Shape*sa,const JPH::SubShapeID&,const JPH::Body&b,const JPH::Shape*sb,const JPH::SubShapeID&)const override{
  auto*ba=reinterpret_cast<const BodyMeta*>(a.GetUserData());auto*bb=reinterpret_cast<const BodyMeta*>(b.GetUserData());if(ba&&bb&&ba->actor&&bb->actor&&(ba->actor==bb->actor||pairs.ignored.count(PairFilter::actorPair(ba->actor,bb->actor))))return false;
  auto*ma=reinterpret_cast<const BodyMeta*>(sa->GetUserData());auto*mb=reinterpret_cast<const BodyMeta*>(sb->GetUserData());
  if(((a.GetObjectLayer()&128)&&!ma)||((b.GetObjectLayer()&128)&&!mb))return true;
  const auto*ca=ma?ma->record:nullptr;const auto*cb=mb?mb->record:nullptr;
  if((ca&&(ca->flags&(Active|Enabled))!=(Active|Enabled))||(cb&&(cb->flags&(Active|Enabled))!=(Active|Enabled)))return false;
  unsigned la=ca?ca->layer:(a.GetObjectLayer()&31),lb=cb?cb->layer:(b.GetObjectLayer()&31);return pairs.masks[la]&(1u<<lb);
 }
};
struct SphereHits final:JPH::CastShapeCollector {
 bool hit=false;JPH::ShapeCastResult closest;
 void AddHit(const JPH::ShapeCastResult&result)override{
  // Match the source SphereCast boundary: objects already overlapping the
  // sphere at its origin do not prevent a later, separate overhead hit.
  if(result.mFraction<=0&&result.mPenetrationDepth>0)return;
  if(!hit||result.mFraction<closest.mFraction){closest=result;hit=true;UpdateEarlyOutFraction(result.GetEarlyOutFraction());}
 }
};
struct Overlaps final:JPH::CollideShapeCollector {
 std::vector<int64_t>&out;const JPH::Body*current=nullptr;bool overflow=false;
 explicit Overlaps(std::vector<int64_t>&values):out(values){}
 void OnBody(const JPH::Body&body)override{current=&body;}
 void AddHit(const JPH::CollideShapeResult&hit)override{auto*m=current?leafMeta(*current,hit.mSubShapeID2):nullptr;if(!m||std::find(out.begin(),out.end(),m->collider)!=out.end())return;if(out.size()>=4096){overflow=true;ForceEarlyOut();return;}out.push_back(m->collider);}
};
struct SmallBoxOverlaps final:JPH::TransformedShapeCollector {
 Vec3 endpointA,endpointB;double radiusSquared;std::vector<int64_t>&out;bool overflow=false;
 SmallBoxOverlaps(Vec3 a,Vec3 b,float radius,std::vector<int64_t>&values):endpointA(a),endpointB(b),radiusSquared(double(radius)*radius),out(values){}
 void AddHit(const JPH::TransformedShape&shape)override{
  if(shape.mShape->GetSubType()!=JPH::EShapeSubType::Box)return;auto*meta=reinterpret_cast<const BodyMeta*>(shape.mShape->GetUserData());if(!meta||std::find(out.begin(),out.end(),meta->collider)!=out.end())return;
  auto inverse=shape.GetInverseCenterOfMassTransform();auto a=inverse*j(endpointA),b=inverse*j(endpointB);auto extent=static_cast<const JPH::BoxShape*>(shape.mShape.GetPtr())->GetHalfExtent()*shape.GetShapeScale().Abs();
  double start[]={a.GetX(),a.GetY(),a.GetZ()},direction[]={b.GetX()-a.GetX(),b.GetY()-a.GetY(),b.GetZ()-a.GetZ()},half[]={extent.GetX(),extent.GetY(),extent.GetZ()},cuts[8]={0,1};unsigned count=2;
  for(unsigned i=0;i<3;i++)if(direction[i]!=0)for(double sign:{-1.,1.}){double t=(sign*half[i]-start[i])/direction[i];if(t>0&&t<1)cuts[count++]=t;}std::sort(cuts,cuts+count);
  auto distance=[&](double t){double sum=0;for(unsigned i=0;i<3;i++){double delta=std::max(0.,std::abs(start[i]+direction[i]*t)-half[i]);sum+=delta*delta;}return sum;};double minimum=distance(0);
  for(unsigned interval=1;interval<count;interval++){double lo=cuts[interval-1],hi=cuts[interval],mid=(lo+hi)*.5,quadratic=0,linear=0;for(unsigned i=0;i<3;i++){double p=start[i]+direction[i]*mid;if(p>half[i]||p<-half[i]){double bound=p>0?half[i]:-half[i];quadratic+=direction[i]*direction[i];linear+=direction[i]*(start[i]-bound);}}double t=quadratic>0?std::clamp(-linear/quadratic,lo,hi):lo;minimum=std::min({minimum,distance(lo),distance(hi),distance(t)});}
  if(minimum>radiusSquared)return;if(out.size()>=4096){overflow=true;ForceEarlyOut();return;}out.push_back(meta->collider);
 }
};
float combine(float a,float b,uint32_t mode){switch(mode){case 1:return std::min(a,b);case 2:return a*b;case 3:return std::max(a,b);default:return (a+b)*.5f;}}
float friction(const JPH::Body&a,const JPH::SubShapeID&sa,const JPH::Body&b,const JPH::SubShapeID&sb){
 auto*ma=leafMeta(a,sa);auto*mb=leafMeta(b,sb);return combine(ma&&ma->record?ma->record->dynamicFriction:a.GetFriction(),mb&&mb->record?mb->record->dynamicFriction:b.GetFriction(),std::max(ma?ma->frictionMode:0u,mb?mb->frictionMode:0u));
}
float restitution(const JPH::Body&a,const JPH::SubShapeID&sa,const JPH::Body&b,const JPH::SubShapeID&sb){
 auto*ma=leafMeta(a,sa);auto*mb=leafMeta(b,sb);return combine(ma&&ma->record?ma->record->restitution:a.GetRestitution(),mb&&mb->record?mb->record->restitution:b.GetRestitution(),std::max(ma?ma->bounceMode:0u,mb?mb->bounceMode:0u));
}
JPH::RefConst<JPH::Shape> createShape(const ColliderRecord&c,const PhysicsPack&pack,std::string&error,uint64_t userData=0,bool rootBoxFrame=false){
 JPH::Shape::ShapeResult result;
 if(c.kind==Box){
  Vec3 axes[3]={{c.world[0],c.world[4],c.world[8]},{c.world[1],c.world[5],c.world[9]},{c.world[2],c.world[6],c.world[10]}};float scale[3];bool primitive=true,zero=false;
  for(unsigned i=0;i<3;i++){scale[i]=float(std::hypot(double(axes[i].x),double(axes[i].y),double(axes[i].z)));if(!std::isfinite(scale[i])){error="box scale overflow";return {};}if(scale[i]>0)axes[i]={axes[i].x/scale[i],axes[i].y/scale[i],axes[i].z/scale[i]};else{zero=true;axes[i]=i==0?Vec3{1,0,0}:i==1?Vec3{0,1,0}:Vec3{0,0,1};}}
  if(zero){primitive=rootBoxFrame;for(unsigned i=0;i<3;i++)if(scale[i]>0){Vec3 expected=i==0?Vec3{1,0,0}:i==1?Vec3{0,1,0}:Vec3{0,0,1};if(std::abs(dot(axes[i],expected))<.9999f)primitive=false;}}
  for(unsigned i=0;i<3;i++)for(unsigned k=i+1;k<3;k++)if(std::abs(dot(axes[i],axes[k]))>.0001f)primitive=false;
  if(primitive){
   if(dot(cross(axes[0],axes[1]),axes[2])<0)axes[2]=axes[2]*-1.f;JPH::Mat44 basis(JPH::Vec4(j(axes[0]),0),JPH::Vec4(j(axes[1]),0),JPH::Vec4(j(axes[2]),0),JPH::Vec4(0,0,0,1));
   // Unity 2022.3's actual physics queries clamp box half-extents to FLT_EPSILON,
   // including authored zero-scale hand colliders. This is not an empty shape.
   Vec3 halfValue{float(std::max(double(FLT_EPSILON),double(c.size[0])*scale[0]*.5)),float(std::max(double(FLT_EPSILON),double(c.size[1])*scale[1]*.5)),float(std::max(double(FLT_EPSILON),double(c.size[2])*scale[2]*.5))},center=c.point({c.center[0],c.center[1],c.center[2]});if(!finite(halfValue)||!finite(center)){error="box extent/center overflow";return {};}auto box=JPH::BoxShapeSettings(j(halfValue),0).Create();if(box.HasError()){error=box.GetError().c_str();return {};}box.Get()->SetUserData(userData);auto positioned=JPH::RotatedTranslatedShapeSettings(j(center),basis.GetQuaternion(),box.Get()).Create();if(positioned.HasError()){error=positioned.GetError().c_str();return {};}positioned.Get()->SetUserData(userData);return positioned.Get();
  }
 }
 if(c.kind==Box || (c.kind==Mesh && (c.flags&Convex))){
  JPH::Array<JPH::Vec3> points;
  if(c.kind==Box){
   for(unsigned corner=0;corner<8;corner++)points.push_back(j(c.point({c.center[0]+((corner&1)?1:-1)*c.size[0]*.5f,c.center[1]+((corner&2)?1:-1)*c.size[1]*.5f,c.center[2]+((corner&4)?1:-1)*c.size[2]*.5f})));
  }else for(uint32_t i=0;i<c.vertexCount;i++)points.push_back(j(c.point(pack.vertices[c.firstVertex+i])));
  JPH::ConvexHullShapeSettings settings(points);settings.mMaxConvexRadius=0;result=settings.Create();
 }else if(c.kind==Mesh){
  JPH::TriangleList triangles;
  for(uint32_t i=0;i<c.indexCount;i+=3){
   triangles.emplace_back(j(c.point(pack.vertices[pack.indices[c.firstIndex+i]])),j(c.point(pack.vertices[pack.indices[c.firstIndex+i+1]])),j(c.point(pack.vertices[pack.indices[c.firstIndex+i+2]])));
  }
  result=JPH::MeshShapeSettings(triangles).Create();
 }else{
  Vec3 axes[3]={{c.world[0],c.world[4],c.world[8]},{c.world[1],c.world[5],c.world[9]},{c.world[2],c.world[6],c.world[10]}};
  float scale[3];for(unsigned a=0;a<3;a++){scale[a]=std::sqrt(dot(axes[a],axes[a]));if(scale[a]<1e-6f){error="zero-scale primitive collider";return {};}}
  for(unsigned a=0;a<3;a++)for(unsigned b=a+1;b<3;b++)if(std::abs(dot(axes[a],axes[b])/(scale[a]*scale[b]))>.001f){error="sheared round collider requires a verified Unity scale adapter";return {};}
  JPH::RefConst<JPH::Shape> inner;auto rotation=JPH::Quat::sIdentity();
  if(c.kind==Sphere)result=JPH::SphereShapeSettings(c.radius*std::max({scale[0],scale[1],scale[2]})).Create();
  else{
   unsigned axis=unsigned(c.size[0]);float radius=c.radius*std::max(scale[(axis+1)%3],scale[(axis+2)%3]);float cylinder=std::max(0.f,c.height*scale[axis]*.5f-radius);
   result=JPH::CapsuleShapeSettings(cylinder,radius).Create();rotation=JPH::Quat::sFromTo(JPH::Vec3::sAxisY(),j(axes[axis]) / scale[axis]);
  }
  if(result.HasError()){error=result.GetError().c_str();return {};}
  result.Get()->SetUserData(userData);inner=result.Get();result=JPH::RotatedTranslatedShapeSettings(j(c.point({c.center[0],c.center[1],c.center[2]})),rotation,inner).Create();
 }
 if(result.HasError()){error=result.GetError().c_str();return {};}
 result.Get()->SetUserData(userData);return result.Get();
}
}

struct PhysicsWorld::Impl {
 Lifetime lifetime;
 BroadLayers broad;BroadFilter broadFilter;PairFilter pairs;SimulationShapes simulationShapes{pairs};
 JPH::TempAllocatorImpl temporary{16*1024*1024};
 std::unique_ptr<JPH::JobSystem> jobs;
 JPH::PhysicsSystem system;
 std::vector<JPH::BodyID> bodies;std::vector<BodyMeta> metadata;
 PhysicsPack definitions;
 std::unordered_map<int64_t,size_t> colliderIndex;
 std::unordered_map<int64_t,JPH::BodyID> staticBodies;
 struct RuntimeActor {uint32_t definition=0;bool active=false,kinematic=false,gravity=false,forceQueued=false;float mass=1;Vec3 forceDelta{},forceAcceleration{};JPH::BodyID root;std::vector<JPH::BodyID>sensors;ActorPose pose;BodyMeta metadata{};};
 ActorPack actorDefinitions;std::unordered_map<int64_t,RuntimeActor> actors;std::vector<int64_t> activeActors;
 std::vector<int64_t> forceActors;
 struct RuntimeJoint {bool enabled=false,broken=false,attached=false,dirty=false;JPH::Ref<JPH::TwoBodyConstraint> constraint;JPH::Vec3 ownerAnchor,connectedAnchor,axisOwner,secondaryOwner,axisConnected,secondaryConnected;};
 JointPack jointDefinitions;bool jointsReady=false;std::vector<RuntimeJoint> joints;std::unordered_map<int64_t,uint32_t> jointIds;std::unordered_map<int64_t,std::vector<uint32_t>> actorJoints;std::vector<uint32_t> dirtyJoints,activeJoints;std::vector<int64_t> brokenJoints;
 JPH::BodyID player;PlayerSpec specification;PhysicsCoverage coverage;
 float playerHeight=0;Vec3 groundOffset{};
 JPH::RefConst<JPH::Shape> standingShape,crouchedShape;
 ColliderRecord sensors[2]{};bool hasSensor[2]{};
 Vec3 queuedDelta{},queuedForce{};
 bool invalidMovement=false,topologyDirty=false;
 void dirtyActorJoints(int64_t id){auto found=actorJoints.find(id);if(found==actorJoints.end())return;for(auto index:found->second)if(!joints[index].dirty){joints[index].dirty=true;dirtyJoints.push_back(index);}}
 void detachJoint(uint32_t index){auto&state=joints[index];if(!state.attached)return;system.RemoveConstraint(state.constraint);state.attached=false;const auto&j=jointDefinitions.joints[index];if(!(j.flags&JointCollide)&&j.connected){auto key=PairFilter::actorPair(j.body,j.connected);auto found=pairs.ignored.find(key);if(found!=pairs.ignored.end()&&!--found->second)pairs.ignored.erase(found);}activeJoints.erase(std::remove(activeJoints.begin(),activeJoints.end(),index),activeJoints.end());coverage.activeJoints--;topologyDirty=true;}
 void detachActorJoints(int64_t id){auto found=actorJoints.find(id);if(found==actorJoints.end())return;for(auto index:found->second)detachJoint(index);dirtyActorJoints(id);}
 bool refreshJoints(std::string&error);
 bool finishJoints(float,std::string&error);
 bool createActor(RuntimeActor&actor,std::string&error){
  const auto&a=actorDefinitions.bodies[actor.definition];
  if((a.flags&ActorHasJoint)&&(!jointsReady||!actorJoints.count(a.id))){error="joint-dependent actor needs the native joint solver";return false;}
  if((a.flags&(ActorImplicitCenter|ActorImplicitTensor))!=(ActorImplicitCenter|ActorImplicitTensor)){error="explicit authored center/inertia adapter pending";return false;}
  size_t metadataStart=metadata.size(),bodiesStart=bodies.size();
  auto rollback=[&]{auto&interface=system.GetBodyInterface();while(bodies.size()>bodiesStart){auto id=bodies.back();if(interface.IsAdded(id))interface.RemoveBody(id);interface.DestroyBody(id);bodies.pop_back();}actor.root=JPH::BodyID();actor.sensors.clear();metadata.resize(metadataStart);};
  JPH::StaticCompoundShapeSettings solid,sensors;unsigned solidCount=0,sensorCount=0;
  auto authoredRotation=JPH::Quat(a.rotation[0],a.rotation[1],a.rotation[2],a.rotation[3]).Normalized();auto inverse=JPH::Mat44::sRotation(authoredRotation.Conjugated());
  for(size_t i=a.firstCollider;i<size_t(a.firstCollider)+a.colliderCount;i++){
   const auto&ref=actorDefinitions.colliders[i];const auto&c=definitions.colliders[ref.index];if(!ref.relativeActive||!(c.flags&Enabled))continue;
   if(c.kind==Mesh&&(!c.vertexCount||!c.indexCount))continue;
   ColliderRecord local=c;Vec3 translation{c.world[3]-a.position.x,c.world[7]-a.position.y,c.world[11]-a.position.z};auto t=inverse*j(translation);
   for(unsigned axis=0;axis<3;axis++){auto column=inverse*JPH::Vec3(c.world[axis],c.world[4+axis],c.world[8+axis]);local.world[axis]=column.GetX();local.world[4+axis]=column.GetY();local.world[8+axis]=column.GetZ();}local.world[3]=t.GetX();local.world[7]=t.GetY();local.world[11]=t.GetZ();
   metadata.push_back({c.id,c.frictionCombine,c.bounceCombine,&c});auto shape=createShape(local,definitions,error,reinterpret_cast<uintptr_t>(&metadata.back()),c.objectId==a.objectId);if(!shape){rollback();return false;}
   if(!actor.kinematic&&!(c.flags&Trigger)&&shape->MustBeStatic()){error="dynamic nonconvex mesh requires an authored convex decomposition";rollback();return false;}
   if(c.flags&Trigger){sensors.AddShape(JPH::Vec3::sZero(),JPH::Quat::sIdentity(),shape);sensorCount++;}else{solid.AddShape(JPH::Vec3::sZero(),JPH::Quat::sIdentity(),shape);solidCount++;}
  }
  auto compound=[&](JPH::StaticCompoundShapeSettings&settings,unsigned count)->JPH::RefConst<JPH::Shape>{if(!count)return new JPH::EmptyShape;auto result=settings.Create();if(result.HasError()){error=result.GetError().c_str();return {};}return result.Get();};
  bool allSensors=!solidCount&&sensorCount;auto rootShape=compound(allSensors?sensors:solid,allSensors?sensorCount:solidCount);if(!rootShape){rollback();return false;}
  auto makeBody=[&](JPH::RefConst<JPH::Shape> shape,bool auxiliary,bool sensor)->JPH::BodyID{
   bool kinematic=auxiliary||actor.kinematic||a.constraints==126;auto rotation=JPH::Quat(actor.pose.rotation[0],actor.pose.rotation[1],actor.pose.rotation[2],actor.pose.rotation[3]).Normalized();
   JPH::BodyCreationSettings settings(shape,j(actor.pose.position),rotation,kinematic?JPH::EMotionType::Kinematic:JPH::EMotionType::Dynamic,JPH::ObjectLayer(a.layer|32|128|(sensor?64:0)));
   settings.mIsSensor=sensor;settings.mAllowSleeping=!kinematic;settings.mGravityFactor=actor.gravity&&!auxiliary?1:0;settings.mLinearDamping=a.drag;settings.mAngularDamping=a.angularDrag;
   settings.mAllowDynamicOrKinematic=true;actor.metadata={0,0,0,nullptr,a.id};settings.mUserData=reinterpret_cast<uintptr_t>(&actor.metadata);
   settings.mAllowedDOFs=kinematic?JPH::EAllowedDOFs::All:JPH::EAllowedDOFs(63u&~(a.constraints>>1));settings.mMotionQuality=!kinematic&&a.ccd?JPH::EMotionQuality::LinearCast:JPH::EMotionQuality::Discrete;
   settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;settings.mMassPropertiesOverride.mMass=actor.mass;
   auto id=system.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::Activate);if(!id.IsInvalid())bodies.push_back(id);return id;
  };
  actor.root=makeBody(rootShape,false,allSensors);if(actor.root.IsInvalid()){error="actor body allocation budget";rollback();return false;}
  if(solidCount&&sensorCount){auto shape=compound(sensors,sensorCount);if(!shape){rollback();return false;}auto id=makeBody(shape,true,true);if(id.IsInvalid()){error="actor sensor allocation budget";rollback();return false;}actor.sensors.push_back(id);}
  system.GetBodyInterface().SetLinearAndAngularVelocity(actor.root,a.constraints==126?JPH::Vec3::sZero():j(actor.pose.velocity),a.constraints==126?JPH::Vec3::sZero():j(actor.pose.angularVelocity));
  coverage.actorBodies++;coverage.actorColliders+=solidCount+sensorCount;return true;
 }
 bool createStatic(const ColliderRecord&c,std::string&error){
  if(c.kind==Mesh&&(!c.vertexCount||!c.indexCount))return true;
  metadata.push_back({c.id,c.frictionCombine,c.bounceCombine,&c});auto shape=createShape(c,definitions,error,reinterpret_cast<uintptr_t>(&metadata.back()));if(!shape){metadata.pop_back();error="collider "+std::to_string(c.id)+": "+error;return false;}
  JPH::BodyCreationSettings settings(shape,JPH::RVec3::sZero(),JPH::Quat::sIdentity(),JPH::EMotionType::Static,JPH::ObjectLayer(c.layer|((c.flags&Trigger)?64:0)));
  settings.mIsSensor=bool(c.flags&Trigger);settings.mFriction=c.dynamicFriction;settings.mRestitution=c.restitution;
  settings.mUserData=reinterpret_cast<uintptr_t>(&metadata.back());
  auto&interface=system.GetBodyInterface();auto id=interface.CreateAndAddBody(settings,JPH::EActivation::DontActivate);
  if(id.IsInvalid()){metadata.pop_back();error="physics body budget exhausted";return false;}
  bodies.push_back(id);staticBodies.emplace(c.id,id);coverage.staticBodies++;coverage.sensors+=settings.mIsSensor;return true;
 }
 ~Impl(){while(!activeJoints.empty())detachJoint(activeJoints.back());joints.clear();auto&interface=system.GetBodyInterface();for(auto id:bodies){if(interface.IsAdded(id))interface.RemoveBody(id);interface.DestroyBody(id);}}
};
#include "nx_physics_joints.inc"
PhysicsWorld::PhysicsWorld()=default;
PhysicsWorld::~PhysicsWorld()=default;
bool PhysicsWorld::initialize(const PhysicsPack&pack,unsigned workers,std::string&error){
 const char*validation=nullptr;if(!pack.validate(&validation)){error=validation;return false;}
 if(workers>2){error="native physics worker budget exceeds available application cores";return false;}
 auto next=std::make_unique<Impl>();next->specification=pack.player;next->pairs.masks=pack.layerMasks;next->definitions=pack;
 for(size_t i=0;i<pack.colliders.size();i++)next->colliderIndex.emplace(pack.colliders[i].id,i);
 if(workers)next->jobs=std::make_unique<JPH::JobSystemThreadPool>(2048,8,int(workers));else next->jobs=std::make_unique<JPH::JobSystemSingleThreaded>(2048);
 next->system.Init(16384,0,65536,16384,next->broad,next->broadFilter,next->pairs);
 next->system.SetGravity(j(pack.player.gravity));next->system.SetCombineFriction(friction);next->system.SetCombineRestitution(restitution);next->system.SetSimShapeFilter(&next->simulationShapes);
 next->metadata.reserve(pack.colliders.size()+1);next->bodies.reserve(pack.colliders.size()+1);
 auto&interface=next->system.GetBodyInterface();
 for(const auto&c:pack.colliders){if(c.id==pack.player.groundCollider){next->sensors[0]=c;next->hasSensor[0]=true;}if(c.id==pack.player.slopeCollider){next->sensors[1]=c;next->hasSensor[1]=true;}}
 for(const auto&c:pack.colliders){
  if((c.flags&(Active|Enabled))!=(Active|Enabled)){next->coverage.inactive++;continue;}
  if(c.flags&AttachedBody){if(c.objectId!=pack.player.objectId||c.kind!=Capsule)next->coverage.dynamicDeferred++;continue;}
  if(c.kind==Mesh && (!c.vertexCount||!c.indexCount)){next->coverage.emptyMesh++;continue;}
  if(!next->createStatic(next->definitions.colliders[next->colliderIndex.at(c.id)],error)){next->coverage.unsupported++;return false;}
 }
 auto&p=pack.player;JPH::CapsuleShapeSettings capsule(std::max(0.f,p.height*.5f-p.radius),p.radius);auto capsuleResult=capsule.Create();
 if(capsuleResult.HasError()){error=capsuleResult.GetError().c_str();return false;}
 JPH::RotatedTranslatedShapeSettings offset(j(p.center),JPH::Quat::sIdentity(),capsuleResult.Get());auto shape=offset.Create();
 if(shape.HasError()){error=shape.GetError().c_str();return false;}
 Vec3 right=cross(p.up,p.forward);JPH::Mat44 rotation(JPH::Vec4(j(right),0),JPH::Vec4(j(p.up),0),JPH::Vec4(j(p.forward),0),JPH::Vec4(0,0,0,1));
 JPH::BodyCreationSettings settings(shape.Get(),j(p.position),rotation.GetQuaternion(),JPH::EMotionType::Dynamic,34);
 settings.mAllowedDOFs=JPH::EAllowedDOFs::TranslationX|JPH::EAllowedDOFs::TranslationY|JPH::EAllowedDOFs::TranslationZ;
 settings.mMotionQuality=JPH::EMotionQuality::LinearCast;settings.mLinearDamping=0;settings.mAngularDamping=0;settings.mFriction=p.dynamicFriction;settings.mRestitution=p.restitution;settings.mMaxLinearVelocity=1000;
 settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;settings.mMassPropertiesOverride.mMass=p.mass;
 next->metadata.push_back({0,p.frictionCombine,p.bounceCombine});settings.mUserData=reinterpret_cast<uintptr_t>(&next->metadata.back());
 next->standingShape=shape.Get();next->playerHeight=p.height;
 next->player=interface.CreateAndAddBody(settings,JPH::EActivation::Activate);
 if(next->player.IsInvalid()){error="player body allocation";return false;}
 next->bodies.push_back(next->player);next->system.OptimizeBroadPhase();impl=std::move(next);error.clear();return true;
}
PhysicsCoverage PhysicsWorld::coverage()const{return impl?impl->coverage:PhysicsCoverage{};}
bool PhysicsWorld::initializeActors(const ActorPack&source,const GraphPack&definitions,const WorldGraph&graph,std::string&error){
 if(!impl||!impl->actors.empty()){error="actor attachment needs a fresh initialized physics world";return false;}
 if(!source.bind(impl->definitions,definitions,graph,error))return false;
 if(impl->jointsReady&&impl->jointDefinitions.header.actorCrc!=actorChecksum(source)){error="loaded joint pack belongs to a different actor pack";return false;}
 auto&state=*impl;size_t bodyStart=state.bodies.size(),metaStart=state.metadata.size();auto coverage=state.coverage;state.actorDefinitions=source;
 for(uint32_t i=0;i<source.bodies.size();i++){const auto&a=source.bodies[i];Impl::RuntimeActor actor;actor.definition=i;actor.mass=a.mass;actor.gravity=bool(a.flags&ActorGravity);actor.pose.position=a.position;std::copy(a.rotation,a.rotation+4,actor.pose.rotation);actor.active=bool(a.flags&ActorActive);actor.kinematic=bool(a.flags&ActorKinematic);if(a.flags&ActorPlayer)actor.root=state.player;state.actors.emplace(a.id,std::move(actor));}
 for(auto&entry:state.actors){auto&a=entry.second;const auto&record=source.bodies[a.definition];if(!a.active||(record.flags&ActorPlayer))continue;if(!state.createActor(a,error)){
   auto&interface=state.system.GetBodyInterface();while(state.bodies.size()>bodyStart){auto id=state.bodies.back();if(interface.IsAdded(id))interface.RemoveBody(id);interface.DestroyBody(id);state.bodies.pop_back();}state.metadata.resize(metaStart);state.actors.clear();state.activeActors.clear();state.actorDefinitions={};state.coverage=coverage;return false;
  }}
 // The remaining deferred active colliders belong to player helper systems.
 for(const auto&entry:state.actors)if(entry.second.active){state.activeActors.push_back(entry.first);state.dirtyActorJoints(entry.first);if(source.bodies[entry.second.definition].flags&ActorPlayer)reinterpret_cast<BodyMeta*>(state.system.GetBodyInterface().GetUserData(state.player))->actor=entry.first;}
 state.coverage.dynamicDeferred=coverage.dynamicDeferred>=state.coverage.actorColliders?coverage.dynamicDeferred-state.coverage.actorColliders:0;error.clear();return true;
}
bool PhysicsWorld::actorActive(int64_t id)const{if(!impl)return false;auto it=impl->actors.find(id);return it!=impl->actors.end()&&it->second.active;}
bool PhysicsWorld::actorKinematic(int64_t id)const{if(!impl)return false;auto it=impl->actors.find(id);return it!=impl->actors.end()&&it->second.kinematic;}
bool PhysicsWorld::hasActor(int64_t id)const{return impl&&impl->actors.count(id);}
const std::vector<int64_t>&PhysicsWorld::activeActorIds()const{static const std::vector<int64_t> empty;return impl?impl->activeActors:empty;}
bool PhysicsWorld::setActorActive(int64_t id,bool active,std::string&error){
 if(!impl){error="actor physics world not initialized";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=it->second;const auto&a=impl->actorDefinitions.bodies[actor.definition];if(a.flags&ActorPlayer){error="player activation uses the player lifecycle";return false;}if(actor.active==active){error.clear();return true;}
 auto&interface=impl->system.GetBodyInterface();if(active){
  if(actor.root.IsInvalid()){if(!impl->createActor(actor,error))return false;}
  else{interface.AddBody(actor.root,JPH::EActivation::Activate);interface.SetLinearAndAngularVelocity(actor.root,j(actor.pose.velocity),j(actor.pose.angularVelocity));for(auto sensor:actor.sensors)interface.AddBody(sensor,JPH::EActivation::Activate);}
 }else{
  if(!actorPose(id,actor.pose,error))return false;actor.forceDelta=actor.forceAcceleration={};impl->detachActorJoints(id);if(!actor.root.IsInvalid()){interface.RemoveBody(actor.root);for(auto sensor:actor.sensors)interface.RemoveBody(sensor);}
 }
 actor.active=active;if(active)impl->activeActors.push_back(id);else impl->activeActors.erase(std::remove(impl->activeActors.begin(),impl->activeActors.end(),id),impl->activeActors.end());for(size_t i=a.firstCollider;i<size_t(a.firstCollider)+a.colliderCount;i++){const auto&ref=impl->actorDefinitions.colliders[i];auto&c=impl->definitions.colliders[ref.index];c.flags=(c.flags&~uint32_t(Active))|((active&&ref.relativeActive)?Active:0u);}impl->dirtyActorJoints(id);impl->topologyDirty=true;error.clear();return true;
}
bool PhysicsWorld::actorPose(int64_t id,ActorPose&out,std::string&error)const{
 if(!impl){error="actor world not initialized";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}const auto&actor=it->second;out=actor.pose;
 if(actor.active&&!actor.root.IsInvalid()){const auto&interface=impl->system.GetBodyInterface();out.position=n(interface.GetPosition(actor.root));out.velocity=n(interface.GetLinearVelocity(actor.root));out.angularVelocity=n(interface.GetAngularVelocity(actor.root));auto q=interface.GetRotation(actor.root);out.rotation[0]=q.GetX();out.rotation[1]=q.GetY();out.rotation[2]=q.GetZ();out.rotation[3]=q.GetW();}error.clear();return true;
}
bool PhysicsWorld::setActorPose(int64_t id,const ActorPose&pose,std::string&error){
 float length=0;for(auto v:pose.rotation){if(!std::isfinite(v)){error="nonfinite actor rotation";return false;}length+=v*v;}
 if(!impl||!finite(pose.position)||!finite(pose.velocity)||!finite(pose.angularVelocity)||std::abs(length-1)>.001f){error="invalid actor pose";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=it->second;const auto&a=impl->actorDefinitions.bodies[actor.definition];if(a.flags&ActorPlayer){error="player teleport uses the player lifecycle";return false;}
 actor.pose=pose;if(a.constraints==126)actor.pose.velocity=actor.pose.angularVelocity={};if(!actor.root.IsInvalid()){
  auto&interface=impl->system.GetBodyInterface();auto rotation=JPH::Quat(pose.rotation[0],pose.rotation[1],pose.rotation[2],pose.rotation[3]).Normalized();interface.SetPositionAndRotation(actor.root,j(pose.position),rotation,actor.active?JPH::EActivation::Activate:JPH::EActivation::DontActivate);interface.SetLinearAndAngularVelocity(actor.root,j(actor.pose.velocity),j(actor.pose.angularVelocity));for(auto sensor:actor.sensors)interface.SetPositionAndRotation(sensor,j(pose.position),rotation,actor.active?JPH::EActivation::Activate:JPH::EActivation::DontActivate);
 }error.clear();return true;
}
bool PhysicsWorld::moveActorKinematic(int64_t id,Vec3 position,const float rotation[4],float dt,std::string&error){
 float length=0;for(unsigned i=0;i<4;i++){if(!std::isfinite(rotation[i])){error="nonfinite kinematic rotation";return false;}length+=rotation[i]*rotation[i];}
 if(!impl||!finite(position)||std::abs(length-1)>.001f||!std::isfinite(dt)||std::abs(dt-.008f)>1e-7f){error="invalid kinematic target/tick";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=it->second;const auto&a=impl->actorDefinitions.bodies[actor.definition];if(!actor.kinematic||a.flags&ActorPlayer||!actor.active||actor.root.IsInvalid()){error="kinematic target requires an active authored kinematic body";return false;}
 auto q=JPH::Quat(rotation[0],rotation[1],rotation[2],rotation[3]).Normalized();impl->system.GetBodyInterface().MoveKinematic(actor.root,j(position),q,dt);for(auto sensor:actor.sensors)impl->system.GetBodyInterface().MoveKinematic(sensor,j(position),q,dt);error.clear();return true;
}
bool PhysicsWorld::setActorKinematic(int64_t id,bool kinematic,std::string&error){
 if(!impl){error="actor world not initialized";return false;}auto found=impl->actors.find(id);if(found==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=found->second;const auto&a=impl->actorDefinitions.bodies[actor.definition];if(a.flags&ActorPlayer){error="player motion mode uses the player lifecycle";return false;}if(actor.kinematic==kinematic){error.clear();return true;}bool solverKinematic=kinematic||a.constraints==126;
 if(!actor.root.IsInvalid()){
  auto&interface=impl->system.GetBodyInterface();if(!kinematic){JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(),actor.root);if(!lock.Succeeded()){error="actor motion mode lock failed";return false;}if(lock.GetBody().GetShape()->MustBeStatic()){error="dynamic nonconvex actor shape unsupported";return false;}}
  interface.SetMotionType(actor.root,solverKinematic?JPH::EMotionType::Kinematic:JPH::EMotionType::Dynamic,actor.active?JPH::EActivation::Activate:JPH::EActivation::DontActivate);
  JPH::BodyLockWrite lock(impl->system.GetBodyLockInterface(),actor.root);if(!lock.Succeeded()){error="actor mass update lock failed";return false;}auto&body=lock.GetBody();auto mass=body.GetShape()->GetMassProperties();mass.ScaleToMass(actor.mass);body.GetMotionProperties()->SetMassProperties(solverKinematic?JPH::EAllowedDOFs::All:JPH::EAllowedDOFs(63u&~(a.constraints>>1)),mass);body.SetAllowSleeping(!solverKinematic);body.SetLinearVelocity(JPH::Vec3::sZero());body.SetAngularVelocity(JPH::Vec3::sZero());
 }actor.pose.velocity=actor.pose.angularVelocity=actor.forceDelta=actor.forceAcceleration={};actor.kinematic=kinematic;impl->dirtyActorJoints(id);impl->topologyDirty=true;error.clear();return true;
}
#include "nx_physics_forces.inc"
bool PhysicsWorld::step(float seconds,std::string&error){
 if(!impl||!std::isfinite(seconds)||std::abs(seconds-.008f)>1e-7f){error="physics requires initialized world and 8 ms fixed tick";return false;}
 if(impl->invalidMovement||!finite(impl->queuedDelta)||!finite(impl->queuedForce)){error="nonfinite movement command";return false;}
 auto&interface=impl->system.GetBodyInterface();
 if(!impl->refreshJoints(error))return false;
 if(impl->topologyDirty){for(auto body:impl->bodies)if(interface.IsAdded(body)&&(interface.GetObjectLayer(body)&32)){interface.InvalidateContactCache(body);interface.ActivateBody(body);}impl->topologyDirty=false;}
 for(auto id:impl->forceActors){auto&a=impl->actors.at(id);auto delta=a.forceDelta+a.forceAcceleration*seconds;if(!finite(delta)||!finite(delta*a.mass)){error="actor force integration overflow";return false;}}
 for(auto id:impl->forceActors){auto&a=impl->actors.at(id);if(a.active&&!a.kinematic&&!a.root.IsInvalid())interface.AddImpulse(a.root,j((a.forceDelta+a.forceAcceleration*seconds)*a.mass));a.forceDelta=a.forceAcceleration={};a.forceQueued=false;}impl->forceActors.clear();
 interface.AddImpulse(impl->player,j(impl->queuedDelta)*impl->specification.mass);interface.AddForce(impl->player,j(impl->queuedForce));impl->queuedDelta={};impl->queuedForce={};
 auto result=impl->system.Update(seconds,1,&impl->temporary,impl->jobs.get());
 if(result!=JPH::EPhysicsUpdateError::None){error="physics update capacity error "+std::to_string(unsigned(result));return false;}
 if(!impl->finishJoints(seconds,error))return false;
 if(!finite(playerPosition())||!finite(playerVelocity())){error="nonfinite player physics state";return false;}
 for(auto id:impl->activeActors){auto&actor=impl->actors.at(id);auto position=interface.GetPosition(actor.root),velocity=interface.GetLinearVelocity(actor.root);if(!finite(n(position))||!finite(n(velocity))){error="nonfinite actor physics state";return false;}if(!actor.sensors.empty()){auto rotation=interface.GetRotation(actor.root);for(auto sensor:actor.sensors)interface.SetPositionAndRotationWhenChanged(sensor,position,rotation,JPH::EActivation::Activate);}}
 error.clear();return true;
}
Vec3 PhysicsWorld::playerPosition()const{return impl?n(impl->system.GetBodyInterface().GetPosition(impl->player)):Vec3{};}
Vec3 PhysicsWorld::playerVelocity()const{return impl?n(impl->system.GetBodyInterface().GetLinearVelocity(impl->player)):Vec3{};}
void PhysicsWorld::setPlayerVelocity(Vec3 velocity){if(impl&&finite(velocity))impl->system.GetBodyInterface().SetLinearVelocity(impl->player,j(velocity));}
bool PhysicsWorld::setPlayerLayer(unsigned layer,std::string&error){
 if(!impl||layer>=32){error="invalid player collision layer";return false;}
 auto&interface=impl->system.GetBodyInterface();JPH::ObjectLayer desired=JPH::ObjectLayer(layer|32);
 if(interface.GetObjectLayer(impl->player)!=desired){interface.SetObjectLayer(impl->player,desired);for(auto&c:impl->definitions.colliders)if(c.objectId==impl->specification.objectId)c.layer=layer;}
 error.clear();return true;
}
unsigned PhysicsWorld::playerLayer()const{return impl?unsigned(impl->system.GetBodyInterface().GetObjectLayer(impl->player)&31):32;}
const ColliderRecord*PhysicsWorld::colliderMetadata(int64_t id)const{
 if(!impl)return nullptr;auto it=impl->colliderIndex.find(id);return it==impl->colliderIndex.end()?nullptr:&impl->definitions.colliders[it->second];
}
const ColliderRecord*PhysicsWorld::colliderAt(uint32_t index)const{return impl&&index<impl->definitions.colliders.size()?&impl->definitions.colliders[index]:nullptr;}
bool PhysicsWorld::staticColliderActive(int64_t id)const{
 if(!impl)return false;auto it=impl->staticBodies.find(id);return it!=impl->staticBodies.end()&&impl->system.GetBodyInterface().IsAdded(it->second);
}
bool PhysicsWorld::setStaticColliderActive(int64_t id,bool active,std::string&error){
 const auto*c=colliderMetadata(id);return setStaticColliderState(id,active,active||bool(c&&(c->flags&Enabled)),error);
}
bool PhysicsWorld::setStaticColliderState(int64_t id,bool hierarchyActive,bool enabled,std::string&error){
 if(!impl){error="physics world not initialized";return false;}auto record=impl->colliderIndex.find(id);
 if(record==impl->colliderIndex.end()){error="unknown authored collider";return false;}auto&c=impl->definitions.colliders[record->second];
 if(c.flags&AttachedBody){error="collider belongs to a dynamic/kinematic body; static activation adapter cannot handle it";return false;}
 bool active=hierarchyActive&&enabled;auto&interface=impl->system.GetBodyInterface();auto it=impl->staticBodies.find(id);
 bool changed=false;
 if(active&&it==impl->staticBodies.end()){if(!impl->createStatic(c,error))return false;changed=impl->staticBodies.find(id)!=impl->staticBodies.end();}
 else if(it!=impl->staticBodies.end()){
  bool added=interface.IsAdded(it->second);
  if(active&&!added){interface.AddBody(it->second,JPH::EActivation::DontActivate);changed=true;}
  else if(!active&&added){interface.RemoveBody(it->second);changed=true;}
 }
 c.flags=(c.flags&~uint32_t(Active|Enabled))|(hierarchyActive?Active:0u)|(enabled?Enabled:0u);
 // Batch wake/cache work once per simulation step, including removal of a
 // support under a sleeping body. Enabling a whole room stays linear.
 impl->topologyDirty=impl->topologyDirty||changed;
 error.clear();return true;
}
bool PhysicsWorld::setLayerCollision(unsigned a,unsigned b,bool enabled,std::string&error){
 if(!impl||a>=32||b>=32){error="invalid collision matrix layer";return false;}
 bool old=bool(impl->pairs.masks[a]&(1u<<b));if(old==enabled){error.clear();return true;}
 if(enabled){impl->pairs.masks[a]|=1u<<b;impl->pairs.masks[b]|=1u<<a;}
 else{impl->pairs.masks[a]&=~(1u<<b);impl->pairs.masks[b]&=~(1u<<a);}
 auto&interface=impl->system.GetBodyInterface();
 for(auto body:impl->bodies){unsigned layer=interface.GetObjectLayer(body);if((layer&32)&&((layer&31)==a||(layer&31)==b)){interface.InvalidateContactCache(body);interface.ActivateBody(body);}}
 error.clear();return true;
}
float PhysicsWorld::playerHeight()const{return impl?impl->playerHeight:0;}
Vec3 PhysicsWorld::playerCapsuleBottom()const{
 if(!impl)return {};
 // The migrated player has locked rotation. Return the world AABB's bottom
 // center used by HandleSlideState, including the capsule's authored center.
 JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(),impl->player);if(!lock.Succeeded())return {};
 const auto&bounds=lock.GetBody().GetWorldSpaceBounds();auto center=bounds.GetCenter();return {center.GetX(),bounds.mMin.GetY(),center.GetZ()};
}
Vec3 PhysicsWorld::groundProbePosition(bool slope)const{
 unsigned index=slope?1:0;if(!impl||!impl->hasSensor[index])return {};
 const auto&c=impl->sensors[index];return c.point({})+playerPosition()-impl->specification.position+(slope?Vec3{}:impl->groundOffset);
}
bool PhysicsWorld::setPlayerCapsule(float height,Vec3 delta,Vec3 groundOffset,std::string&error){
 if(!impl||!std::isfinite(height)||height<2*impl->specification.radius||height>1000||!finite(delta)||!finite(groundOffset)||!finite(playerPosition()+delta)){error="invalid player capsule pose";return false;}
 JPH::RefConst<JPH::Shape> shape;
 if(height==impl->specification.height)shape=impl->standingShape;
 else if(height==1.25f&&impl->crouchedShape)shape=impl->crouchedShape;
 else{
  JPH::CapsuleShapeSettings capsule(height*.5f-impl->specification.radius,impl->specification.radius);auto result=capsule.Create();
  if(result.HasError()){error=result.GetError().c_str();return false;}
  JPH::RotatedTranslatedShapeSettings offset(j(impl->specification.center),JPH::Quat::sIdentity(),result.Get());auto adjusted=offset.Create();
  if(adjusted.HasError()){error=adjusted.GetError().c_str();return false;}
  shape=adjusted.Get();if(height==1.25f)impl->crouchedShape=shape;
 }
 auto&interface=impl->system.GetBodyInterface();
 if(height!=impl->playerHeight)interface.SetShape(impl->player,shape.GetPtr(),false,JPH::EActivation::Activate);
 if(dot(delta,delta)>0)interface.SetPosition(impl->player,j(playerPosition()+delta),JPH::EActivation::Activate);
 impl->playerHeight=height;impl->groundOffset=groundOffset;error.clear();return true;
}
void PhysicsWorld::applyMovement(const MovementResult&r){
 if(!impl)return;
 if(!finite(r.velocity)||!finite(r.queuedVelocityChange)||!finite(r.queuedForce)){impl->invalidMovement=true;return;}
 auto&interface=impl->system.GetBodyInterface();if(r.velocityWritten)interface.SetLinearVelocity(impl->player,j(r.velocity));
 interface.SetGravityFactor(impl->player,r.useGravity?1.f:0.f);impl->queuedDelta=impl->queuedDelta+r.queuedVelocityChange;impl->queuedForce=impl->queuedForce+r.queuedForce;
}
RayHit PhysicsWorld::ray(Vec3 start,Vec3 displacement,uint32_t mask,bool triggers)const{
 RayHit out;if(!impl||!finite(start)||!finite(displacement)||dot(displacement,displacement)<=0)return out;
 JPH::RRayCast ray(j(start),j(displacement));JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> hits;JPH::RayCastSettings settings;settings.mTreatConvexAsSolid=false;
 QueryLayers layers(mask,triggers);JPH::IgnoreSingleBodyFilter ignore(impl->player);
 QueryShapes shapes(mask,triggers);impl->system.GetNarrowPhaseQuery().CastRay(ray,settings,hits,{},layers,ignore,shapes);if(!hits.HadHit())return out;const auto&hit=hits.mHit;
 JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(),hit.mBodyID);if(!lock.Succeeded())return out;
 const auto&body=lock.GetBody();out.hit=true;out.point=n(ray.GetPointOnRay(hit.mFraction));out.normal=n(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,j(out.point)));out.distance=std::sqrt(dot(displacement,displacement))*hit.mFraction;
 auto*meta=leafMeta(body,hit.mSubShapeID2);out.collider=meta?meta->collider:0;return out;
}
bool PhysicsWorld::sphereCast(Vec3 start,float radius,Vec3 displacement,uint32_t mask,bool triggers,RayHit&out,std::string&error)const{
 out={};float square=dot(displacement,displacement);
 if(!impl||!finite(start)||!finite(displacement)||!std::isfinite(radius)||radius<=0||!std::isfinite(square)||square<=0){error="invalid sphere cast";return false;}
 JPH::SphereShape shape(radius);JPH::RShapeCast cast(&shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(j(start)),j(displacement));JPH::ShapeCastSettings settings;settings.mReturnDeepestPoint=true;settings.mCollisionTolerance=std::min(settings.mCollisionTolerance,radius*.001f);SphereHits hits;
 QueryLayers layers(mask,triggers);JPH::IgnoreSingleBodyFilter ignore(impl->player);
 QueryShapes shapes(mask,triggers);impl->system.GetNarrowPhaseQuery().CastShape(cast,settings,JPH::RVec3::sZero(),hits,{},layers,ignore,shapes);
 if(hits.hit){
  JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(),hits.closest.mBodyID2);if(!lock.Succeeded()){error="sphere cast body disappeared";return false;}
  out.hit=true;auto*meta=leafMeta(lock.GetBody(),hits.closest.mSubShapeID2);out.collider=meta?meta->collider:0;out.distance=std::sqrt(square)*hits.closest.mFraction;
  out.point=n(hits.closest.mContactPointOn2);out.normal=n((-hits.closest.mPenetrationAxis).NormalizedOr(JPH::Vec3::sAxisY()));
 }
 error.clear();return true;
}
bool PhysicsWorld::overlapCapsule(Vec3 a,Vec3 b,float radius,uint32_t mask,bool triggers,std::vector<int64_t>&out,std::string&error)const{
 out.clear();if(!impl||!finite(a)||!finite(b)||!std::isfinite(radius)||radius<=0){error="invalid capsule query";return false;}
 auto axis=j(b-a);float length=axis.Length();auto center=j(a+(b-a)*.5f);
 QueryLayers layers(mask,triggers);JPH::IgnoreSingleBodyFilter ignore(impl->player);Overlaps collector(out);JPH::CollideShapeSettings settings;settings.mCollisionTolerance=std::min(settings.mCollisionTolerance,radius*.001f);
 QueryShapes shapes(mask,triggers);auto query=[&](const JPH::Shape*shape,JPH::QuatArg rotation){impl->system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sRotationTranslation(rotation,center),settings,JPH::RVec3::sZero(),collector,{},layers,ignore,shapes);};
 if(length>0){JPH::CapsuleShape shape(length*.5f,radius);query(&shape,JPH::Quat::sFromTo(JPH::Vec3::sAxisY(),axis/length));}
 else{JPH::SphereShape shape(radius);query(&shape,JPH::Quat::sIdentity());}
 // Generic convex GJK loses sub-micrometre box containment even with a small
 // tolerance. Preserve Unity's tiny-box query behavior using exact segment-
 // to-box distance, collected through the same broadphase/layer/shape filters.
 if(radius<.0001f){SmallBoxOverlaps boxes(a,b,radius,out);auto extent=JPH::Vec3::sReplicate(radius);JPH::AABox bounds(JPH::Vec3::sMin(j(a),j(b))-extent,JPH::Vec3::sMax(j(a),j(b))+extent);impl->system.GetNarrowPhaseQuery().CollectTransformedShapes(bounds,boxes,{},layers,ignore,shapes);if(boxes.overflow)collector.overflow=true;}
 if(collector.overflow){out.clear();error="capsule overlap result budget exceeded";return false;}
 error.clear();return true;
}
bool PhysicsWorld::groundOverlaps(bool slope,bool forceUnscaled,std::vector<int64_t>&out,std::string&error)const{
 out.clear();unsigned index=slope?1:0;if(!impl||!impl->hasSensor[index]){error="authored ground sensor is not bound";return false;}
 const auto&c=impl->sensors[index];Vec3 center=c.point({c.center[0],c.center[1],c.center[2]})+(playerPosition()-impl->specification.position)+(slope?Vec3{}:impl->groundOffset);
 float radius=c.radius,halfCylinder=c.height*.5f-radius;Vec3 up=normalized(impl->specification.gravity)*-1.f;
 if(!forceUnscaled){
  Vec3 axes[3]={{c.world[0],c.world[4],c.world[8]},{c.world[1],c.world[5],c.world[9]},{c.world[2],c.world[6],c.world[10]}};float scale[3];
  for(unsigned a=0;a<3;a++){scale[a]=std::sqrt(dot(axes[a],axes[a]));if(scale[a]<1e-6f){error="zero-scale ground sensor";return false;}}
  for(unsigned a=0;a<3;a++)for(unsigned b=a+1;b<3;b++)if(std::abs(dot(axes[a],axes[b])/(scale[a]*scale[b]))>.001f){error="sheared ground sensor";return false;}
  unsigned axis=unsigned(c.size[0]);radius*=std::max(scale[(axis+1)%3],scale[(axis+2)%3]);halfCylinder=std::max(0.f,c.height*scale[axis]*.5f-radius);up=axes[axis]*(1/scale[axis]);
 }
 return overlapCapsule(center+up*halfCylinder,center-up*halfCylinder,radius,forceUnscaled?~0u:impl->pairs.masks[c.layer],true,out,error);
}
}
