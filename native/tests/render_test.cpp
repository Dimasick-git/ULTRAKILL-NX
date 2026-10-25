#include "nx_render.h"
#include <cstdio>
#include <cstdlib>
#include <cfloat>
using namespace nx;using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
void near(float a,float b,float tolerance=.0001f){if(std::abs(a-b)>tolerance){std::fprintf(stderr,"values %.9g %.9g\n",a,b);CHECK(false);}}
GraphPack graphFixture(){
 GraphPack p;const char*names="TransformMeshRenderer";p.strings.assign(names,names+21);
 for(unsigned i=0;i<2;i++){GraphNode n{};n.objectId=10+i;n.transformId=100+i*100;n.parent=-1;n.firstComponent=i*2;n.componentCount=2;n.flags=i?0:1;n.rotation[3]=1;n.scale={1,1,1};p.nodes.push_back(n);p.components.push_back({n.transformId,i,0,9,0,0,1});p.components.push_back({n.transformId+1,i,9,12,0,0,3});}return p;
}
RenderPack fixture(const GraphPack&g){
 RenderPack p;std::memcpy(p.header.magic,"NXRENDER",8);p.header.version=1;p.header.vertexCount=3;p.header.indexCount=6;p.header.ownerCount=2;p.header.chunkCount=2;p.header.cameraMask=1;p.header.fov=75;p.header.graphCrc=g.checksum();
 p.vertices={{{0,0,4},{0,0,-1},{0,0},~0u},{{1,0,4},{0,0,-1},{1,0},~0u},{{0,1,4},{0,0,-1},{0,1},~0u}};p.indices={0,1,2,0,1,2};
 for(unsigned i=0;i<2;i++){p.owners.push_back({101+int64_t(i)*100,10+int64_t(i),i,1,1,0});p.chunks.push_back({i,5,-100,i*3,3,{0,0,4},{1,1,4}});}return p;
}
void fixtures(){
 auto g=graphFixture();WorldGraph graph;std::string error;CHECK(graph.initialize(g,error));auto p=fixture(g);CHECK(p.validate(error));RenderPlan plan;CHECK(plan.initialize(p,g,graph,error));auto camera=p.camera();CHECK(plan.prepare(camera,1,error));CHECK(plan.drawList().size()==1&&plan.drawList()[0].owner==0);
 CHECK(graph.setActive(1,true,error));CHECK(plan.prepare(camera,1,error));CHECK(plan.drawList().size()==2);CHECK(graph.setWorldPosition(1,{1000,0,0},error));CHECK(plan.prepare(camera,1,error));CHECK(plan.drawList().size()==1);near(plan.bounds(1)->minimum[0],1000);
 CHECK(graph.setWorldPosition(0,{0,0,-10},error));CHECK(plan.prepare(camera,1,error)&&plan.drawList().empty());camera.yaw=3.14159265359f;CHECK(plan.prepare(camera,1,error)&&plan.drawList().size()==1);CHECK(graph.setLayer(0,5,error));CHECK(plan.prepare(camera,1,error)&&plan.drawList().empty());CHECK(graph.setLayer(0,0,error));CHECK(graph.setComponentEnabled(1,false,error));CHECK(plan.prepare(camera,1,error)&&plan.drawList().empty());CHECK(graph.setComponentEnabled(1,true,error));
 float q[4]={0,0,0,1};CHECK(graph.setLocal(0,{},q,{2,3,4},error));camera.yaw=0;CHECK(plan.prepare(camera,1,error)&&plan.drawList().size()==1);near(plan.uniform(0).normalWorld.m[0],.5f);near(plan.uniform(0).normalWorld.m[5],1.f/3);near(plan.uniform(0).normalWorld.m[10],.25f);near(plan.bounds(0)->maximum[0],2);near(plan.bounds(0)->maximum[1],3);near(plan.bounds(0)->minimum[2],16);
 CHECK(graph.setLocal(0,{},q,{0,1,1},error));CHECK(plan.prepare(camera,1,error)&&plan.drawList().empty());CHECK(!plan.prepare(camera,0,error));
 auto bad=p;bad.header.graphCrc^=1;CHECK(!plan.initialize(bad,g,graph,error));bad=p;bad.chunks[0].owner=1;CHECK(!bad.validate(error));bad=p;bad.owners[1].rendererId=bad.owners[0].rendererId;CHECK(!bad.validate(error));bad=p;bad.indices[0]=100;CHECK(!bad.validate(error));bad=p;bad.chunks[0].minimum[0]=NAN;CHECK(!bad.validate(error));
 std::puts("PASS: render owner binding, inherited/enabled/layer visibility, moved-object culling, cached world bounds, inverse-transpose normals, degenerate transforms and hierarchy identity validation");
}
void corruption(const char*path){
 FILE*f=std::fopen(path,"rb");CHECK(f);std::fseek(f,0,SEEK_END);long length=std::ftell(f);std::rewind(f);std::vector<uint8_t> original(length);CHECK(std::fread(original.data(),1,original.size(),f)==original.size());std::fclose(f);
 auto reject=[](const std::vector<uint8_t>&bytes){FILE*f=std::tmpfile();CHECK(f);CHECK(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());std::rewind(f);RenderPack p;std::string error;CHECK(!p.read(f,error));std::fclose(f);};
 auto refresh=[](std::vector<uint8_t>&bytes){reinterpret_cast<RenderHeader*>(bytes.data())->crc=crc32(bytes.data()+sizeof(RenderHeader),bytes.size()-sizeof(RenderHeader));};
 auto bad=original;bad.pop_back();reject(bad);bad=original;reinterpret_cast<RenderHeader*>(bad.data())->ownerCount=UINT32_MAX;reject(bad);
 for(unsigned i=0;i<24;i++){bad=original;bad[sizeof(RenderHeader)+(i*104729)%(bad.size()-sizeof(RenderHeader))]^=1;reject(bad);}
 bad=original;auto*h=reinterpret_cast<RenderHeader*>(bad.data());auto offset=sizeof(RenderHeader)+size_t(h->vertexCount)*sizeof(Vertex)+size_t(h->indexCount)*4;reinterpret_cast<RenderOwner*>(bad.data()+offset)->objectId=0;refresh(bad);reject(bad);
 std::puts("PASS: owned render pack budgets, payload length, CRC mutations and renderer identity validation");
}
int main(int argc,char**argv){
 fixtures();if(argc<3)return 0;RenderPack p;GraphPack g;WorldGraph graph;RenderPlan plan;std::string error;
 CHECK(p.load(argv[1],error));CHECK(g.load(argv[2],error));CHECK(graph.initialize(g,error));CHECK(plan.initialize(p,g,graph,error));CHECK(plan.prepare(p.camera(),1280.f/720,error));CHECK(!plan.drawList().empty());CHECK(p.owners.size()==3811&&p.vertices.size()==322612&&p.indices.size()/3==252217);
 size_t fixtureOwner=p.owners.size();for(size_t i=0;i<p.owners.size();i++)if(p.owners[i].rendererId==25026)fixtureOwner=i;CHECK(fixtureOwner<p.owners.size());const auto&owner=p.owners[fixtureOwner];const auto&world=graph.node(plan.binding(uint32_t(fixtureOwner))->node)->world;float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
 for(size_t c=owner.firstChunk;c<size_t(owner.firstChunk)+owner.chunkCount;c++){
  const auto&chunk=p.chunks[c];CHECK(chunk.materialFile==5&&chunk.materialId==int64_t(-8740404655613328582LL));
  for(size_t index=chunk.firstIndex;index<size_t(chunk.firstIndex)+chunk.indexCount;index++){
   const auto&v=p.vertices[p.indices[index]];for(unsigned axis=0;axis<3;axis++){float value=world.m[axis]*v.position[0]+world.m[4+axis]*v.position[1]+world.m[8+axis]*v.position[2]+world.m[12+axis];lo[axis]=std::min(lo[axis],value);hi[axis]=std::max(hi[axis],value);}
  }
 }
 near(lo[0],-5,.001f);near(lo[1],-3,.001f);near(lo[2],380.5f,.001f);near(hi[0],-2.5f,.001f);near(hi[1],0,.001f);near(hi[2],381,.001f);
 std::printf("PASS: actual owned render scene: %zu owners, %zu triangles, %zu chunks, %zu initially selected draws; original material references and static-batch/collider bounds retained; GPU/material parity unverified\n",p.owners.size(),p.indices.size()/3,p.chunks.size(),plan.drawList().size());corruption(argv[1]);
}
