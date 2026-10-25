#include "nx_world.h"
#include <utility>
namespace nx::game {
namespace {
Mat4 compose(const GraphNode&n){
 float x=n.rotation[0],y=n.rotation[1],z=n.rotation[2],w=n.rotation[3];Mat4 m;
 m.m[0]=(1-2*(y*y+z*z))*n.scale.x;m.m[1]=2*(x*y+z*w)*n.scale.x;m.m[2]=2*(x*z-y*w)*n.scale.x;
 m.m[4]=2*(x*y-z*w)*n.scale.y;m.m[5]=(1-2*(x*x+z*z))*n.scale.y;m.m[6]=2*(y*z+x*w)*n.scale.y;
 m.m[8]=2*(x*z+y*w)*n.scale.z;m.m[9]=2*(y*z-x*w)*n.scale.z;m.m[10]=(1-2*(x*x+y*y))*n.scale.z;
 m.m[12]=n.position.x;m.m[13]=n.position.y;m.m[14]=n.position.z;m.m[15]=1;return m;
}
bool finiteMatrix(const Mat4&m){for(float v:m.m)if(!std::isfinite(v))return false;return true;}
bool inversePoint(const Mat4&m,Vec3 world,Vec3&out){
 float a=m.m[0],b=m.m[4],c=m.m[8],d=m.m[1],e=m.m[5],f=m.m[9],g=m.m[2],h=m.m[6],i=m.m[10];
 float determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f)return false;
 Vec3 p=world-Vec3{m.m[12],m.m[13],m.m[14]};
 out={(p.x*(e*i-f*h)+p.y*(c*h-b*i)+p.z*(b*f-c*e))/determinant,(p.x*(f*g-d*i)+p.y*(a*i-c*g)+p.z*(c*d-a*f))/determinant,(p.x*(d*h-e*g)+p.y*(b*g-a*h)+p.z*(a*e-b*d))/determinant};return true;
}
void quaternionProduct(const float a[4],const float b[4],float out[4]){
 float r[4]={a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};float length=0;for(auto v:r)length+=v*v;float scale=1/std::sqrt(length);for(unsigned i=0;i<4;i++)out[i]=r[i]*scale;
}
}
bool WorldGraph::validLocal(const GraphNode&n)const{
 for(float v:{n.position.x,n.position.y,n.position.z,n.scale.x,n.scale.y,n.scale.z,n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]})if(!std::isfinite(v))return false;
 float sq=0;for(float v:n.rotation)sq+=v*v;return std::abs(sq-1)<=.002f;
}
bool WorldGraph::update(uint32_t root,const GraphNode&replacement,bool emit,std::string&error){
 scratch.clear();order.clear();scratch.push_back(root);size_t eventCount=0;
 while(!scratch.empty()){
  uint32_t index=scratch.back();scratch.pop_back();order.push_back(index);const auto&n=index==root?replacement:states[index].local;
  Mat4 local=compose(n);const Mat4*parent=&parentFrame.world;bool parentActive=parentFrame.active;
  if(n.parent>=0){parent=index==root?&states[size_t(n.parent)].world:&candidateWorld[size_t(n.parent)];parentActive=index==root?states[size_t(n.parent)].active:bool(candidateActive[size_t(n.parent)]);}
  candidateWorld[index]=parent?multiply(*parent,local):local;candidateActive[index]=bool(n.flags&1)&&parentActive;
  if(!finiteMatrix(candidateWorld[index])){error="hierarchy world transform overflow";return false;}
  if(emit&&states[index].active!=bool(candidateActive[index]))for(size_t c=n.firstComponent;c<size_t(n.firstComponent)+n.componentCount;c++)if(enabled[c])eventCount++;
  for(auto it=children[index].rbegin();it!=children[index].rend();++it)scratch.push_back(*it);
 }
 if(events.size()+eventCount>1048576){error="hierarchy activation queue not being consumed";return false;}
 // Commit only after every descendant's matrix and the event budget passed.
 states[root].local=replacement;
 for(auto index:order){auto&state=states[index];bool active=candidateActive[index];if(emit&&state.active!=active)for(size_t c=state.local.firstComponent;c<size_t(state.local.firstComponent)+state.local.componentCount;c++)if(enabled[c])events.push_back({uint32_t(c),active});state.world=candidateWorld[index];state.active=active;}
 error.clear();return true;
}
bool WorldGraph::initialize(const GraphPack&source,std::string&error){
 if(!source.validate(error))return false;WorldGraph next;next.pack=&source;next.states.resize(source.nodes.size());next.children.resize(source.nodes.size());next.enabled.resize(source.components.size());next.candidateActive.resize(source.nodes.size());next.candidateWorld.resize(source.nodes.size());next.scratch.reserve(source.nodes.size());next.order.reserve(source.nodes.size());
 for(size_t index=0;index<source.nodes.size();index++){next.states[index].local=source.nodes[index];next.objects.emplace(source.nodes[index].objectId,uint32_t(index));if(source.nodes[index].parent>=0)next.children[size_t(source.nodes[index].parent)].push_back(uint32_t(index));}
 for(size_t c=0;c<source.components.size();c++){next.enabled[c]=bool(source.components[c].flags&1);next.componentIds.emplace(source.components[c].id,uint32_t(c));}
 for(size_t index=0;index<source.nodes.size();index++)if(source.nodes[index].parent==-1&&!next.update(uint32_t(index),source.nodes[index],false,error))return false;
 *this=std::move(next);error.clear();return true;
}
bool WorldGraph::setLocal(uint32_t index,Vec3 p,const float q[4],Vec3 s,std::string&error){
 if(index>=states.size()||!q){error="invalid hierarchy node";return false;}
 GraphNode n=states[index].local;n.position=p;n.scale=s;std::copy(q,q+4,n.rotation);
 if(!validLocal(n)){error="invalid hierarchy local transform";return false;}return update(index,n,true,error);
}
bool WorldGraph::setWorldPosition(uint32_t index,Vec3 p,std::string&error){
 if(index>=states.size()){error="invalid hierarchy node";return false;}auto n=states[index].local;
 if(!inversePoint(n.parent>=0?states[size_t(n.parent)].world:parentFrame.world,p,p)){error="singular hierarchy parent transform";return false;}
 n.position=p;if(!validLocal(n)){error="invalid hierarchy world position";return false;}return update(index,n,true,error);
}
bool WorldGraph::worldRotation(uint32_t index,float out[4],std::string&error)const{
 if(index>=states.size()||!out){error="invalid hierarchy rotation node";return false;}float rotation[4]={0,0,0,1};for(int32_t at=int32_t(index);at>=0;at=states[size_t(at)].local.parent)quaternionProduct(states[size_t(at)].local.rotation,rotation,rotation);quaternionProduct(parentFrame.rotation,rotation,rotation);std::copy(rotation,rotation+4,out);error.clear();return true;
}
bool WorldGraph::setWorldPose(uint32_t index,Vec3 p,const float q[4],std::string&error){
 if(index>=states.size()||!q){error="invalid hierarchy pose node";return false;}float length=0;for(unsigned i=0;i<4;i++){if(!std::isfinite(q[i])){error="invalid hierarchy world rotation";return false;}length+=q[i]*q[i];}if(std::abs(length-1)>.002f){error="nonunit hierarchy world rotation";return false;}
 auto n=states[index].local;std::copy(q,q+4,n.rotation);
 if(!inversePoint(n.parent>=0?states[size_t(n.parent)].world:parentFrame.world,p,p)){error="singular hierarchy pose parent";return false;}float parent[4];if(n.parent>=0){if(!worldRotation(uint32_t(n.parent),parent,error))return false;}else std::copy(parentFrame.rotation,parentFrame.rotation+4,parent);for(unsigned i=0;i<3;i++)parent[i]=-parent[i];quaternionProduct(parent,q,n.rotation);
 n.position=p;if(!validLocal(n)){error="invalid hierarchy world pose";return false;}return update(index,n,true,error);
}
bool WorldGraph::setActive(uint32_t index,bool active,std::string&error){
 if(index>=states.size()){error="invalid hierarchy node";return false;}auto n=states[index].local;n.flags=(n.flags&~1u)|(active?1u:0u);return update(index,n,true,error);
}
bool WorldGraph::setLayer(uint32_t index,unsigned layer,std::string&error){
 if(index>=states.size()||layer>=32){error="invalid hierarchy object layer";return false;}states[index].local.layer=layer;error.clear();return true;
}
bool WorldGraph::setComponentEnabled(uint32_t index,bool value,std::string&error){
 if(!pack||index>=enabled.size()||!(pack->components[index].flags&2)){error="component has no enabled property";return false;}
 if(bool(enabled[index])==value){error.clear();return true;}
 if(states[pack->components[index].node].active){if(events.size()>=1048576){error="hierarchy activation queue not being consumed";return false;}events.push_back({index,value});}
 enabled[index]=value;error.clear();return true;
}
bool WorldGraph::setParentLocal(uint32_t index,int32_t parent,std::string&error){
 if(index>=states.size()||parent<-1||(parent>=0&&size_t(parent)>=states.size())){error="invalid hierarchy parent";return false;}
 unsigned ancestorDepth=0;for(int32_t at=parent;at!=-1;at=states[size_t(at)].local.parent){if(at==int32_t(index)||++ancestorDepth>=512){error="hierarchy reparent cycle or depth exceeded";return false;}}
 scratch.clear();scratch.push_back(index);
 while(!scratch.empty()){
  uint32_t at=scratch.back();scratch.pop_back();unsigned depth=ancestorDepth+1;
  for(uint32_t child=at;child!=index;child=uint32_t(states[child].local.parent))if(++depth>512){error="hierarchy reparent depth exceeded";return false;}
  for(auto child:children[at])scratch.push_back(child);
 }
 auto n=states[index].local;int32_t old=n.parent;if(old==parent){error.clear();return true;}n.parent=parent;
 if(!update(index,n,true,error))return false;
 if(old>=0){auto&list=children[size_t(old)];list.erase(std::find(list.begin(),list.end(),index));}
 if(parent>=0)children[size_t(parent)].push_back(index);return true;
}
bool WorldGraph::setParentFrame(const GraphParentFrame&frame,std::string&error){
 float length=0;for(auto v:frame.rotation){if(!std::isfinite(v)){error="nonfinite external parent rotation";return false;}length+=v*v;}
 if(!pack||!finiteMatrix(frame.world)||std::abs(length-1)>.002f||frame.world.m[3]!=0||frame.world.m[7]!=0||frame.world.m[11]!=0||frame.world.m[15]!=1){error="invalid external parent frame";return false;}
 uint32_t root=Missing;for(uint32_t i=0;i<states.size();i++)if(states[i].local.parent==-1){if(root!=Missing){error="external prefab parent requires one hierarchy root";return false;}root=i;}
 if(root==Missing){error="external prefab parent has no hierarchy root";return false;}auto previous=parentFrame;parentFrame=frame;if(!update(root,states[root].local,true,error)){parentFrame=previous;return false;}return true;
}
}
