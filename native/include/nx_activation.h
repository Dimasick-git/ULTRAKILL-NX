#pragma once
#include "nx_world.h"
#include "nx_physics.h"
namespace nx::game {
enum class PendingBackend {DynamicPhysics,StaticTransform,Rendering,Audio,Gameplay,Unknown};
struct PendingActivation {ActivationEvent event;PendingBackend backend;};
struct ActivationCounts {unsigned staticStates=0,pending=0,actorStates=0,jointStates=0;};
// Routes hierarchy/component changes to implemented systems. Pending requests
// stay explicit until their real backend consumes them. No script lifecycle
// or dynamic body support is implied by successful static-state application.
class SceneActivation {
 const GraphPack&definitions;WorldGraph&graph;PhysicsWorld&physics;
 struct StaticChange {int64_t collider;bool hierarchyActive,enabled,previousActive,previousEnabled;};
 std::vector<PendingActivation> pending,staged;
 std::vector<StaticChange> changes;
 std::unordered_set<int64_t> seen;
public:
 SceneActivation(const GraphPack&p,WorldGraph&g,PhysicsWorld&w):definitions(p),graph(g),physics(w){}
 bool flush(ActivationCounts&,std::string&error);
 const std::vector<PendingActivation>& pendingRequests()const{return pending;}
 void clearPendingRequests(){pending.clear();}
};
}
