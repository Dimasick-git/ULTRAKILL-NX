#pragma once
#include "nx_physics.h"
namespace nx::game {
class ActorScene {
 const ActorPack*pack=nullptr;const GraphPack*definitions=nullptr;WorldGraph*graph=nullptr;PhysicsWorld*physics=nullptr;
 struct ShapeBinding {uint32_t node;int64_t collider;Mat4 local;float sourceMagnitude;};
 struct Binding {uint32_t actor,node;std::vector<ShapeBinding> shapes;};
 struct Change {int64_t id;bool oldActive;ActorPose oldPose;};
 std::unordered_map<int64_t,Binding> bindings;std::unordered_set<int64_t> dirty;std::vector<Change> changes;
 bool graphPose(uint32_t,ActorPose&,std::string&)const;
 bool geometryUnchanged(const Binding&,const ActorPose&,std::string&)const;
public:
 bool initialize(const ActorPack&,const GraphPack&,WorldGraph&,PhysicsWorld&,std::string&);
 // Reads activation events without clearing them: other scene backends still
 // need the original requests. Unsupported collider geometry/layer mutations
 // fail before simulation rather than silently using obsolete collision data.
 bool beforeStep(float,std::string&);
 bool afterStep(std::string&);
};
}
