// Included inside nx::game after PhysicsWorld::Impl. Keeps Jolt constraint
// ownership in the same world as contacts, actors and native simulation.
bool PhysicsWorld::initializeJoints(const JointPack&source,const ActorPack&actorPack,const GraphPack&graph,std::string&error){
 if(!impl||!impl->actors.empty()||impl->jointsReady){error="joint definitions must attach before actor instantiation";return false;}
 if(!source.bind(actorPack,graph,error))return false;
 std::unordered_map<int64_t,const ActorRecord*> actorLookup;for(const auto&a:actorPack.bodies)actorLookup.emplace(a.id,&a);
 std::vector<Impl::RuntimeJoint> states(source.joints.size());std::unordered_map<int64_t,uint32_t> ids;std::unordered_map<int64_t,std::vector<uint32_t>> adjacency;
 for(uint32_t i=0;i<source.joints.size();i++){
  const auto&j=source.joints[i];auto&s=states[i];s.enabled=j.flags&JointEnabled;s.ownerAnchor=::nx::game::j(j.anchor);s.connectedAnchor=::nx::game::j(j.connectedAnchor);
  const auto&owner=*actorLookup.at(j.body);auto ownerRotation=JPH::Quat(owner.rotation[0],owner.rotation[1],owner.rotation[2],owner.rotation[3]).Normalized();JPH::Quat connectedRotation=JPH::Quat::sIdentity();JPH::Vec3 connectedPosition=JPH::Vec3::sZero();
  if(j.connected){const auto&connected=*actorLookup.at(j.connected);connectedRotation=JPH::Quat(connected.rotation[0],connected.rotation[1],connected.rotation[2],connected.rotation[3]).Normalized();connectedPosition=::nx::game::j(connected.position);}
  // Keep the initial local frames through activation and kinematic/dynamic
  // changes. Rebuilding them from the current pose would erase joint limits.
  if(j.flags&JointAutoAnchor)s.connectedAnchor=connectedRotation.Conjugated()*(::nx::game::j(owner.position)+ownerRotation*s.ownerAnchor-connectedPosition);
  if(!finite(n(s.ownerAnchor))||!finite(n(s.connectedAnchor))){error="joint anchor initialization overflow";return false;}
  Vec3 axis,secondary;std::string pending;if(jointAxes(j,axis,secondary,pending)){s.axisOwner=::nx::game::j(axis);s.secondaryOwner=::nx::game::j(secondary);s.axisConnected=connectedRotation.Conjugated()*(ownerRotation*s.axisOwner);s.secondaryConnected=connectedRotation.Conjugated()*(ownerRotation*s.secondaryOwner);}
  ids.emplace(j.id,i);adjacency[j.body].push_back(i);if(j.connected)adjacency[j.connected].push_back(i);
 }
 impl->jointDefinitions=source;impl->joints=std::move(states);impl->jointIds=std::move(ids);impl->actorJoints=std::move(adjacency);impl->jointsReady=true;error.clear();return true;
}
bool PhysicsWorld::Impl::refreshJoints(std::string&error){
 if(dirtyJoints.empty()){error.clear();return true;}std::sort(dirtyJoints.begin(),dirtyJoints.end());std::vector<uint32_t> attachedNow;
 for(auto index:dirtyJoints){
  auto&state=joints[index];const auto&record=jointDefinitions.joints[index];auto owner=actors.find(record.body),connected=actors.find(record.connected);
  bool ready=state.enabled&&!state.broken&&owner!=actors.end()&&owner->second.active&&!owner->second.root.IsInvalid()&&(!record.connected||(connected!=actors.end()&&connected->second.active&&!connected->second.root.IsInvalid()));
  if(!ready){detachJoint(index);continue;}if(state.attached)continue;
  if(!jointAdapterSupported(record,error)){error="joint "+std::to_string(record.id)+": "+error;for(auto added:attachedNow)detachJoint(added);return false;}
  if(!state.constraint){
   JPH::BodyID bodies[]={owner->second.root,record.connected?connected->second.root:JPH::BodyID()};JPH::BodyLockMultiWrite lock(system.GetBodyLockInterface(),bodies,record.connected?2:1);auto*body1=lock.GetBody(0);auto*body2=record.connected?lock.GetBody(1):&JPH::Body::sFixedToWorld;if(!body1||!body2){error="joint body lock failed";for(auto added:attachedNow)detachJoint(added);return false;}
   auto point1=state.ownerAnchor-body1->GetShape()->GetCenterOfMass(),point2=state.connectedAnchor-body2->GetShape()->GetCenterOfMass();constexpr float radians=3.14159265358979323846f/180;
   // Unity constrains connected-relative-to-owner twist. Owner is Jolt body 1
   // so low/high limits keep their source signs (verified in the Editor oracle).
   if(record.kind==CharacterJoint){JPH::SwingTwistConstraintSettings settings;settings.mSpace=JPH::EConstraintSpace::LocalToBodyCOM;settings.mPosition1=JPH::RVec3(point1);settings.mPosition2=JPH::RVec3(point2);settings.mTwistAxis1=state.axisOwner;settings.mPlaneAxis1=state.secondaryOwner;settings.mTwistAxis2=state.axisConnected;settings.mPlaneAxis2=state.secondaryConnected;settings.mTwistMinAngle=record.twistLow*radians;settings.mTwistMaxAngle=record.twistHigh*radians;settings.mNormalHalfConeAngle=record.swing1*radians;settings.mPlaneHalfConeAngle=record.swing2*radians;state.constraint=settings.Create(*body1,*body2);}
   else if(record.kind==FixedJoint){JPH::FixedConstraintSettings settings;settings.mSpace=JPH::EConstraintSpace::LocalToBodyCOM;settings.mPoint1=JPH::RVec3(point1);settings.mPoint2=JPH::RVec3(point2);settings.mAxisX1=state.axisOwner;settings.mAxisY1=state.secondaryOwner;settings.mAxisX2=state.axisConnected;settings.mAxisY2=state.secondaryConnected;state.constraint=settings.Create(*body1,*body2);}
   else{JPH::SixDOFConstraintSettings settings;settings.mSpace=JPH::EConstraintSpace::LocalToBodyCOM;settings.mPosition1=JPH::RVec3(point1);settings.mPosition2=JPH::RVec3(point2);settings.mAxisX1=state.axisOwner;settings.mAxisY1=state.secondaryOwner;settings.mAxisX2=state.axisConnected;settings.mAxisY2=state.secondaryConnected;
    for(unsigned axis=0;axis<6;axis++){auto dof=JPH::SixDOFConstraintSettings::EAxis(axis);if(record.motion[axis]==0||(axis<3&&record.motion[axis]==1&&record.linearLimit==0))settings.MakeFixedAxis(dof);else if(record.motion[axis]==2)settings.MakeFreeAxis(dof);else{float low=axis==3?record.twistLow:axis==4?-record.swing1:-record.swing2,high=axis==3?record.twistHigh:axis==4?record.swing1:record.swing2;settings.SetLimitedAxis(dof,low*radians,high*radians);}}
    state.constraint=settings.Create(*body1,*body2);
   }
  }
  if(!state.constraint){error="joint constraint allocation failed";for(auto added:attachedNow)detachJoint(added);return false;}system.AddConstraint(state.constraint);state.attached=true;activeJoints.push_back(index);attachedNow.push_back(index);coverage.activeJoints++;
  if(!(record.flags&JointCollide)&&record.connected)pairs.ignored[PairFilter::actorPair(record.body,record.connected)]++;topologyDirty=true;
 }
 for(auto index:dirtyJoints)joints[index].dirty=false;dirtyJoints.clear();std::sort(activeJoints.begin(),activeJoints.end());error.clear();return true;
}
bool PhysicsWorld::Impl::finishJoints(float dt,std::string&error){
 auto&interface=system.GetBodyInterface();std::vector<uint32_t> broken;
 for(auto index:activeJoints){auto&state=joints[index];const auto&record=jointDefinitions.joints[index];float linear=0,angular=0;
  if(record.kind==CharacterJoint){auto*c=static_cast<JPH::SwingTwistConstraint*>(state.constraint.GetPtr());linear=c->GetTotalLambdaPosition().Length()/dt;angular=JPH::Vec3(c->GetTotalLambdaTwist(),c->GetTotalLambdaSwingY(),c->GetTotalLambdaSwingZ()).Length()/dt;}
  else if(record.kind==FixedJoint){auto*c=static_cast<JPH::FixedConstraint*>(state.constraint.GetPtr());linear=c->GetTotalLambdaPosition().Length()/dt;angular=c->GetTotalLambdaRotation().Length()/dt;}
  else{auto*c=static_cast<JPH::SixDOFConstraint*>(state.constraint.GetPtr());linear=c->GetTotalLambdaPosition().Length()/dt;angular=c->GetTotalLambdaRotation().Length()/dt;}
  if(!std::isfinite(linear)||!std::isfinite(angular)){error="nonfinite joint solver impulse";return false;}
  if(linear>record.breakForce||angular>record.breakTorque){if(brokenJoints.size()>=65536){error="joint break event queue not consumed";return false;}state.broken=true;broken.push_back(index);brokenJoints.push_back(record.id);continue;}
  if(record.projection){auto&owner=actors.at(record.body);auto ownerPosition=interface.GetPosition(owner.root);auto ownerRotation=interface.GetRotation(owner.root);auto pointOwner=ownerPosition+ownerRotation*state.ownerAnchor;auto pointConnected=JPH::RVec3(state.connectedAnchor);RuntimeActor*connected=nullptr;if(record.connected){connected=&actors.at(record.connected);pointConnected=interface.GetPosition(connected->root)+interface.GetRotation(connected->root)*state.connectedAnchor;}auto difference=pointConnected-pointOwner;
   if(!finite(n(difference))){error="nonfinite joint anchor error";return false;}if(difference.LengthSq()>record.projectionDistance*record.projectionDistance){if(!owner.kinematic)interface.SetPositionAndRotation(owner.root,ownerPosition+difference,ownerRotation,JPH::EActivation::Activate);else if(connected&&!connected->kinematic)interface.SetPositionAndRotation(connected->root,interface.GetPosition(connected->root)-difference,interface.GetRotation(connected->root),JPH::EActivation::Activate);}
  }
 }
 for(auto index:broken)detachJoint(index);error.clear();return true;
}
bool PhysicsWorld::setJointEnabled(int64_t id,bool enabled,std::string&error){
 if(!impl){error="joint world not initialized";return false;}auto found=impl->jointIds.find(id);if(found==impl->jointIds.end()){error="unknown authored joint";return false;}auto index=found->second;auto&state=impl->joints[index];if(enabled&&state.broken){error="broken joint needs component recreation";return false;}state.enabled=enabled;if(!enabled)impl->detachJoint(index);if(!state.dirty){state.dirty=true;impl->dirtyJoints.push_back(index);}error.clear();return true;
}
bool PhysicsWorld::jointState(int64_t id,NativeJointState&out,std::string&error)const{
 if(!impl){error="joint world not initialized";return false;}auto found=impl->jointIds.find(id);if(found==impl->jointIds.end()){error="unknown authored joint";return false;}auto index=found->second;const auto&state=impl->joints[index];out={state.enabled,state.attached,state.broken,0,{},{}};const auto&record=impl->jointDefinitions.joints[index];auto owner=impl->actors.find(record.body),connected=impl->actors.find(record.connected);
 if(owner!=impl->actors.end()&&!owner->second.root.IsInvalid()){const auto&interface=impl->system.GetBodyInterface();out.ownerAnchor=n(interface.GetPosition(owner->second.root)+interface.GetRotation(owner->second.root)*state.ownerAnchor);out.connectedAnchor=record.connected&&connected!=impl->actors.end()&&!connected->second.root.IsInvalid()?n(interface.GetPosition(connected->second.root)+interface.GetRotation(connected->second.root)*state.connectedAnchor):n(state.connectedAnchor);out.anchorError=std::sqrt(dot(out.ownerAnchor-out.connectedAnchor,out.ownerAnchor-out.connectedAnchor));}error.clear();return true;
}
const std::vector<int64_t>&PhysicsWorld::jointBreakEvents()const{static const std::vector<int64_t> empty;return impl?impl->brokenJoints:empty;}
void PhysicsWorld::clearJointBreakEvents(){if(impl)impl->brokenJoints.clear();}
