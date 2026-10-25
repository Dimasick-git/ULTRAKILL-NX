#include "nx_world.h"
#include <cstdio>
#include <cstdlib>
#include <cfloat>
using namespace nx;using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b,float tolerance=.0001f){if(std::abs(a-b)>tolerance){std::fprintf(stderr,"values %.9g %.9g\n",a,b);CHECK(false);}}
void position(const WorldGraph&w,uint32_t node,Vec3 expected){auto*m=w.node(node);CHECK(m);near(m->world.m[12],expected.x);near(m->world.m[13],expected.y);near(m->world.m[14],expected.z);}
GraphPack fixture(){
 GraphPack p;auto string=[&](const std::string&s){uint32_t at=uint32_t(p.strings.size());p.strings.insert(p.strings.end(),s.begin(),s.end());return std::pair<uint32_t,uint32_t>{at,uint32_t(s.size())};};
 for(unsigned i=0;i<4;i++){
  auto name=string(i==1?"\xd0\xa2\xd0\xb5\xd1\x81\xd1\x82":"node");GraphNode n{};n.objectId=10+i;n.transformId=100+i*10;n.parent=i==1?0:(i==2?1:-1);n.firstComponent=uint32_t(p.components.size());n.componentCount=2;n.nameOffset=name.first;n.nameLength=name.second;n.flags=i==3?0:1;n.rotation[3]=1;n.scale={1,1,1};
  if(i==0){n.position={10,0,0};n.rotation[1]=n.rotation[3]=.70710677f;n.scale={2,3,4};}if(i==1)n.position={1,2,3};if(i==2)n.position={0,0,1};if(i==3)n.position={-3,0,0};
  auto type=string("Transform");GraphComponent transform{n.transformId,i,type.first,type.second,0,0,1};p.components.push_back(transform);
  type=string("MonoBehaviour");auto script=string("OriginalClass");GraphComponent component{n.transformId+1,i,type.first,type.second,script.first,script.second,i==2?2u:3u};p.components.push_back(component);p.nodes.push_back(n);
 }return p;
}
void fixtures(){
 auto pack=fixture();std::string error;CHECK(pack.validate(error));WorldGraph world;CHECK(world.initialize(pack,error));position(world,1,{22,6,-2});position(world,2,{26,6,-2});CHECK(world.find(12)==2&&world.find(99999)==WorldGraph::Missing);
 CHECK(world.componentActive(0)&&world.componentActive(3)&&!world.componentActive(5)&&!world.componentActive(6));CHECK(world.activationEvents().empty());CHECK(world.findComponent(111)==3&&world.findComponent(-99)==WorldGraph::Missing);
 CHECK(world.setLayer(1,15,error));CHECK(world.node(1)->local.layer==15&&world.node(2)->local.layer==0);CHECK(!world.setLayer(1,32,error));CHECK(world.node(1)->local.layer==15);
 CHECK(world.setActive(0,false,error));CHECK(world.activationEvents().size()==5);for(auto e:world.activationEvents())CHECK(!e.active);world.clearEvents();
 CHECK(world.setComponentEnabled(5,true,error));CHECK(world.activationEvents().empty());CHECK(world.setActive(0,true,error));CHECK(world.activationEvents().size()==6);world.clearEvents();CHECK(world.setActive(0,true,error));CHECK(world.activationEvents().empty());CHECK(!world.setComponentEnabled(0,false,error));
 CHECK(world.setParentLocal(1,3,error));position(world,1,{-2,2,3});position(world,2,{-2,2,4});CHECK(!world.node(1)->active&&world.activationEvents().size()==4);world.clearEvents();
 CHECK(!world.setParentLocal(3,2,error));CHECK(world.node(3)->local.parent==-1);CHECK(world.setParentLocal(1,-1,error));position(world,1,{1,2,3});CHECK(world.node(1)->active&&world.activationEvents().size()==4);world.clearEvents();
 CHECK(world.setWorldPosition(1,{9,8,7},error));position(world,2,{9,8,8});CHECK(world.setParentLocal(1,0,error));CHECK(world.setWorldPosition(1,{5,6,7},error));position(world,1,{5,6,7});
 float q[4]={0,.70710677f,0,.70710677f};CHECK(!world.setLocal(0,{10,0,0},q,{FLT_MAX,FLT_MAX,FLT_MAX},error));position(world,1,{5,6,7});near(world.node(0)->local.scale.x,2);CHECK(world.activationEvents().empty());
 q[0]=q[1]=q[2]=q[3]=0;CHECK(!world.setLocal(0,{},q,{1,1,1},error));q[3]=1;CHECK(world.setLocal(0,{10,0,0},q,{0,1,1},error));auto previous=world.node(1)->local.position;CHECK(!world.setWorldPosition(1,{5,6,7},error));near(world.node(1)->local.position.x,previous.x);
 auto bad=pack;bad.nodes[0].parent=2;CHECK(!bad.validate(error));bad=pack;bad.nodes[1].parent=999;CHECK(!bad.validate(error));bad=pack;bad.nodes[1].nameOffset++;CHECK(!bad.validate(error));bad=pack;bad.components[1].id=bad.nodes[1].objectId;CHECK(!bad.validate(error));bad=pack;bad.nodes[1].rotation[3]=NAN;CHECK(!bad.validate(error));bad=pack;bad.components[0].node=1;CHECK(!bad.validate(error));
 std::puts("PASS: hierarchy TRS/world position, rotated nonuniform parents, inherited/component activation, reparenting, cycle rejection, singular/overflow rollback, UTF-8 ranges and component ownership validation; no script lifecycle claimed");
}
void corrupt(const char*path){
 FILE*f=std::fopen(path,"rb");CHECK(f);std::fseek(f,0,SEEK_END);long size=std::ftell(f);std::rewind(f);std::vector<uint8_t> bytes(size);CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());std::fclose(f);
 auto reject=[](const std::vector<uint8_t>&b){FILE*f=std::tmpfile();CHECK(f);CHECK(std::fwrite(b.data(),1,b.size(),f)==b.size());std::rewind(f);GraphPack p;std::string error;CHECK(!p.read(f,error));std::fclose(f);};
 auto bad=bytes;bad.pop_back();reject(bad);bad=bytes;reinterpret_cast<GraphHeader*>(bad.data())->nodeCount=UINT32_MAX;reject(bad);
 for(unsigned i=0;i<100;i++){bad=bytes;bad[sizeof(GraphHeader)+(i*104729)%(bytes.size()-sizeof(GraphHeader))]^=1;reject(bad);}
 auto refresh=[](std::vector<uint8_t>&b){reinterpret_cast<GraphHeader*>(b.data())->crc=crc32(b.data()+sizeof(GraphHeader),b.size()-sizeof(GraphHeader));};
 bad=bytes;auto*n=reinterpret_cast<GraphNode*>(bad.data()+sizeof(GraphHeader));n[0].parent=0;refresh(bad);reject(bad);
 std::puts("PASS: hierarchy pack exact-length/budget/CRC validation and payload mutations");
}
int main(int argc,char**argv){
 fixtures();if(argc<2)return 0;GraphPack pack;std::string error;if(!pack.load(argv[1],error)){std::fprintf(stderr,"graph: %s\n",error.c_str());CHECK(false);}corrupt(argv[1]);WorldGraph world;CHECK(world.initialize(pack,error));CHECK(world.size()==13567&&pack.components.size()==44607);unsigned active=0;for(uint32_t i=0;i<world.size();i++)active+=world.node(i)->active;CHECK(active==899);position(world,world.find(4656),{0,105,253});position(world,world.find(4737),{0,103.744f,253});
 std::printf("PASS: actual level hierarchy: %zu nodes, %zu owned components, %u active; original player/ground transforms retained; physics/render/lifecycle bindings remain incomplete\n",world.size(),pack.components.size(),active);
}
