// Runtime Rigidbody force boundaries. Included inside nx::game after Impl.
float PhysicsWorld::actorMass(int64_t id)const{if(!impl)return 0;auto it=impl->actors.find(id);return it==impl->actors.end()?0:it->second.mass;}
bool PhysicsWorld::actorUsesGravity(int64_t id)const{if(!impl)return false;auto it=impl->actors.find(id);return it!=impl->actors.end()&&it->second.gravity;}
bool PhysicsWorld::setActorMass(int64_t id,float value,std::string&error){
 if(!impl||!std::isfinite(value)||value<=0||!std::isfinite(2.5f/value)){error="invalid or unrepresentable actor mass";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=it->second;const auto&a=impl->actorDefinitions.bodies[actor.definition];if(a.flags&ActorPlayer){error="player mass uses the player lifecycle";return false;}
 if(!actor.root.IsInvalid()){JPH::BodyLockWrite lock(impl->system.GetBodyLockInterface(),actor.root);if(!lock.Succeeded()){error="actor mass lock failed";return false;}auto&body=lock.GetBody();auto properties=body.GetShape()->GetMassProperties();properties.ScaleToMass(value);for(unsigned col=0;col<3;col++)if(!finite(n(properties.mInertia.GetColumn3(col)))){error="actor inertia overflow";return false;}body.GetMotionProperties()->SetMassProperties(actor.kinematic||a.constraints==126?JPH::EAllowedDOFs::All:JPH::EAllowedDOFs(63u&~(a.constraints>>1)),properties);}
 actor.mass=value;error.clear();return true;
}
bool PhysicsWorld::setActorGravity(int64_t id,bool enabled,std::string&error){
 if(!impl){error="actor world not initialized";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=it->second;if(impl->actorDefinitions.bodies[actor.definition].flags&ActorPlayer){error="player gravity uses the player lifecycle";return false;}
 if(!actor.root.IsInvalid())impl->system.GetBodyInterface().SetGravityFactor(actor.root,enabled?1.f:0.f);actor.gravity=enabled;error.clear();return true;
}
bool PhysicsWorld::addActorForce(int64_t id,Vec3 value,ActorForceMode mode,std::string&error){
 if(!impl||!finite(value)||(mode!=ActorForceMode::Force&&mode!=ActorForceMode::Impulse&&mode!=ActorForceMode::VelocityChange&&mode!=ActorForceMode::Acceleration)){error="invalid actor force/mode";return false;}auto it=impl->actors.find(id);if(it==impl->actors.end()){error="unknown authored actor";return false;}auto&actor=it->second;const auto&a=impl->actorDefinitions.bodies[actor.definition];if(a.flags&ActorPlayer){error="player forces use the player movement adapter";return false;}
 if(!actor.active||actor.kinematic||a.constraints==126||(value.x==0&&value.y==0&&value.z==0)){error.clear();return true;}
 // Unity converts Force/Impulse using mass at the call, before a subsequent
 // mass setter or velocity write. Application remains deferred until Simulate.
 Vec3 delta=actor.forceDelta,acceleration=actor.forceAcceleration;
 if(mode==ActorForceMode::Force||mode==ActorForceMode::Impulse)value={value.x/actor.mass,value.y/actor.mass,value.z/actor.mass};
 if(mode==ActorForceMode::Force||mode==ActorForceMode::Acceleration)acceleration=acceleration+value;else delta=delta+value;
 if(!finite(delta)||!finite(acceleration)){error="actor force accumulation overflow";return false;}
 if(!actor.forceQueued){impl->forceActors.push_back(id);actor.forceQueued=true;}actor.forceDelta=delta;actor.forceAcceleration=acceleration;error.clear();return true;
}
