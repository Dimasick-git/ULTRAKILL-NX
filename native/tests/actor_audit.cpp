// Development coverage audit: forcing authored inactive objects on is not a
// gameplay scenario. Failures identify initialization/shape adapters still
// needed; this audit must not be presented as complete gameplay validation.
#include "nx_physics.h"
#include <fstream>
#include <cstdio>
using namespace nx::game;
std::string quote(const std::string&s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){char b[8];std::snprintf(b,sizeof b,"\\u%04x",c);out+=b;}else out+=char(c);}return out+'"';}
int main(int argc,char**argv){
 if(argc!=6){std::fprintf(stderr,"usage: nx_actor_audit ACTORS JOINTS PHYSICS GRAPH REPORT\n");return 1;}ActorPack a;JointPack j;PhysicsPack p;GraphPack g;WorldGraph graph;PhysicsWorld world;std::string error;const char*physicsError=nullptr;
 if(!a.load(argv[1],error)||!j.load(argv[2],error)||!p.load(argv[3],&physicsError)||!g.load(argv[4],error)||!graph.initialize(g,error)||!world.initialize(p,0,error)||!world.initializeJoints(j,a,g,error)||!world.initializeActors(a,g,graph,error)){std::fprintf(stderr,"audit setup failed: %s %s\n",error.c_str(),physicsError?physicsError:"");return 1;}
 struct Failure{int64_t id;std::string error;};std::vector<Failure> shapes,constraints;unsigned shapesPassed=0,jointsPassed=0;
 for(const auto&body:a.bodies){if(body.flags&ActorPlayer)continue;bool old=world.actorActive(body.id);if(!old&&!world.setActorActive(body.id,true,error)){shapes.push_back({body.id,error});continue;}shapesPassed++;if(!old&&!world.setActorActive(body.id,false,error)){std::fprintf(stderr,"audit deactivation failed: %s\n",error.c_str());return 1;}}
 if(!world.step(.008f,error)){std::fprintf(stderr,"audit pending flush failed: %s\n",error.c_str());return 1;}
 for(const auto&joint:j.joints){bool ownerOld=world.actorActive(joint.body),connectedOld=world.actorActive(joint.connected);bool ok=true;
  if(!ownerOld&&!world.setActorActive(joint.body,true,error))ok=false;
  if(ok&&joint.connected&&!connectedOld&&!world.setActorActive(joint.connected,true,error))ok=false;
  if(ok&&!world.step(.008f,error))ok=false;
  NativeJointState state;if(ok&&(!world.jointState(joint.id,state,error)||!state.attached)){ok=false;if(error.empty())error="source joint did not attach";}
  if(ok)jointsPassed++;else constraints.push_back({joint.id,error});
  if(!ownerOld&&world.actorActive(joint.body)&&!world.setActorActive(joint.body,false,error))return 1;
  if(joint.connected&&!connectedOld&&world.actorActive(joint.connected)&&!world.setActorActive(joint.connected,false,error))return 1;
 }
 std::ofstream out(argv[5]);if(!out)return 1;out<<"{\n  \"scope\": \"forced authored-body shape creation and individual source joint attachment; not script initialization, ragdoll animation or gameplay parity\",\n  \"shape_bodies_passed\": "<<shapesPassed<<",\n  \"joint_attachments_passed\": "<<jointsPassed<<",\n  \"shape_failures\": [";for(size_t i=0;i<shapes.size();i++)out<<(i?",":"")<<"{\"body\": \""<<shapes[i].id<<"\", \"error\": "<<quote(shapes[i].error)<<"}";out<<"],\n  \"joint_failures\": [";for(size_t i=0;i<constraints.size();i++)out<<(i?",":"")<<"{\"joint\": \""<<constraints[i].id<<"\", \"error\": "<<quote(constraints[i].error)<<"}";out<<"],\n  \"console_verified\": false,\n  \"gameplay_complete\": false\n}\n";
 std::printf("Actor coverage audit: %u supported bodies, %zu pending body adapters; %u source joint attachments, %zu failures. Not full ragdoll/gameplay verification.\n",shapesPassed,shapes.size(),jointsPassed,constraints.size());return shapes.empty()&&constraints.empty()?0:2;
}
