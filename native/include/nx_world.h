#pragma once
#include "nx_graph.h"
#include <unordered_map>
namespace nx::game {
struct GraphState {GraphNode local{};Mat4 world{};bool active=false;};
struct ActivationEvent {uint32_t component;bool active;};
struct GraphParentFrame {
 Mat4 world{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
 float rotation[4]{0,0,0,1};bool active=true;
};
// Authored hierarchy and component activation state. These events are requests
// for actual systems; this class does not run Unity scripts or their lifecycle.
class WorldGraph {
 const GraphPack*pack=nullptr;
 std::vector<GraphState> states;
 std::vector<std::vector<uint32_t>> children;
 std::vector<uint8_t> enabled,candidateActive;
 std::vector<Mat4> candidateWorld;
 std::vector<uint32_t> scratch,order;
 std::vector<ActivationEvent> events;
 std::unordered_map<int64_t,uint32_t> objects;
 std::unordered_map<int64_t,uint32_t> componentIds;
 GraphParentFrame parentFrame;
 bool update(uint32_t,const GraphNode&,bool emit,std::string&);
 bool validLocal(const GraphNode&)const;
public:
 static constexpr uint32_t Missing=UINT32_MAX;
 bool initialize(const GraphPack&,std::string&);
 uint32_t find(int64_t objectId)const{auto it=objects.find(objectId);return it==objects.end()?Missing:it->second;}
 uint32_t findComponent(int64_t id)const{auto it=componentIds.find(id);return it==componentIds.end()?Missing:it->second;}
 const GraphState*node(uint32_t index)const{return index<states.size()?&states[index]:nullptr;}
 size_t size()const{return states.size();}
 bool componentActive(uint32_t index)const{return pack&&index<enabled.size()&&enabled[index]&&states[pack->components[index].node].active;}
 bool componentEnabled(uint32_t index)const{return index<enabled.size()&&enabled[index];}
 bool setLocal(uint32_t,Vec3,const float rotation[4],Vec3,std::string&);
 bool setWorldPosition(uint32_t,Vec3,std::string&);
 bool worldRotation(uint32_t,float rotation[4],std::string&)const;
 bool setWorldPose(uint32_t,Vec3,const float rotation[4],std::string&);
 bool setActive(uint32_t,bool,std::string&);
 bool setLayer(uint32_t,unsigned,std::string&);
 bool setComponentEnabled(uint32_t,bool,std::string&);
 // Explicitly retains local TRS. World-position-stays reparenting and runtime
 // cloning/destruction require their own validated adapters.
 bool setParentLocal(uint32_t,int32_t parent,std::string&);
 // A prefab has one root, but its parent can live in the level or another
 // instance. Retain the full parent matrix (including shear) and the separate
 // hierarchy quaternion, rather than decomposing/approximating its TRS.
 bool setParentFrame(const GraphParentFrame&,std::string&);
 const std::vector<ActivationEvent>& activationEvents()const{return events;}
 void clearEvents(){events.clear();}
};
}
