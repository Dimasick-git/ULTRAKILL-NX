#pragma once
#include "nx_scene.h"
#include <unordered_set>
#include <string>
#include <algorithm>
namespace nx::game {
struct GraphHeader {char magic[8];uint32_t version,nodeCount,componentCount,stringBytes,crc,reserved;};
struct GraphNode {
 int64_t objectId,transformId;int32_t parent;
 uint32_t firstComponent,componentCount,nameOffset,nameLength,flags,layer,tag;
 Vec3 position;float rotation[4];Vec3 scale;
};
struct GraphComponent {int64_t id;uint32_t node,typeOffset,typeLength,scriptOffset,scriptLength,flags;};
static_assert(sizeof(GraphHeader)==32&&sizeof(GraphNode)==88&&sizeof(GraphComponent)==32,"graph ABI");
class GraphPack {
 bool stringRange(uint32_t offset,uint32_t length)const{
  uint64_t end=uint64_t(offset)+length;if(end>strings.size())return false;
  return (offset==strings.size()||(uint8_t(strings[offset])&0xc0)!=0x80)&&(end==strings.size()||(uint8_t(strings[size_t(end)])&0xc0)!=0x80);
 }
 static bool utf8(const std::vector<char>&s){
  size_t i=0;while(i<s.size()){
   unsigned c=uint8_t(s[i++]);if(c<128)continue;
   unsigned count=0,code=0,minimum=0;
   if(c>=0xc2&&c<=0xdf){count=1;code=c&31;minimum=0x80;}
   else if(c>=0xe0&&c<=0xef){count=2;code=c&15;minimum=0x800;}
   else if(c>=0xf0&&c<=0xf4){count=3;code=c&7;minimum=0x10000;}else return false;
   if(i+count>s.size())return false;
   while(count--){unsigned d=uint8_t(s[i++]);if((d&0xc0)!=0x80)return false;code=(code<<6)|(d&63);}
   if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
  }return true;
 }
public:
 std::vector<GraphNode> nodes;std::vector<GraphComponent> components;std::vector<char> strings;
 uint32_t checksum()const{uint32_t crc=crc32(nodes.data(),nodes.size()*sizeof(GraphNode));crc=crc32(components.data(),components.size()*sizeof(GraphComponent),crc);return crc32(strings.data(),strings.size(),crc);}
 std::string text(uint32_t offset,uint32_t length)const{return length&&stringRange(offset,length)?std::string(strings.data()+offset,length):std::string{};}
 bool load(const char*path,std::string&error){
  FILE*f=std::fopen(path,"rb");if(!f){error="cannot open hierarchy pack";return false;}
  bool ok=read(f,error);std::fclose(f);if(!ok){nodes.clear();components.clear();strings.clear();}return ok;
 }
 bool read(FILE*f,std::string&error){
  error="invalid hierarchy header";GraphHeader h{};
  if(std::fread(&h,sizeof h,1,f)!=1||std::memcmp(h.magic,"NXGRAPH\0",8)||h.version!=1||h.reserved||!h.nodeCount||h.nodeCount>65536||h.componentCount<h.nodeCount||h.componentCount>524288||h.stringBytes>16777216)return false;
  uint64_t bytes=uint64_t(h.nodeCount)*sizeof(GraphNode)+uint64_t(h.componentCount)*sizeof(GraphComponent)+h.stringBytes;
  long start=std::ftell(f);if(start<0||std::fseek(f,0,SEEK_END)||std::ftell(f)<0||uint64_t(std::ftell(f)-start)!=bytes||std::fseek(f,start,SEEK_SET))return false;
  nodes.resize(h.nodeCount);components.resize(h.componentCount);strings.resize(h.stringBytes);error="truncated hierarchy payload";
  if(std::fread(nodes.data(),sizeof(GraphNode),nodes.size(),f)!=nodes.size()||std::fread(components.data(),sizeof(GraphComponent),components.size(),f)!=components.size()||std::fread(strings.data(),1,strings.size(),f)!=strings.size())return false;
  uint32_t crc=crc32(nodes.data(),nodes.size()*sizeof(GraphNode));crc=crc32(components.data(),components.size()*sizeof(GraphComponent),crc);crc=crc32(strings.data(),strings.size(),crc);
  if(crc!=h.crc){error="hierarchy checksum mismatch";return false;}return validate(error);
 }
 bool validate(std::string&error)const{
  error="hierarchy allocation budget";if(nodes.empty()||nodes.size()>65536||components.size()<nodes.size()||components.size()>524288||strings.size()>16777216)return false;
  error="invalid hierarchy string encoding";if(!utf8(strings))return false;
  std::unordered_set<int64_t> ids;size_t nextComponent=0;
  for(size_t index=0;index<nodes.size();index++){
   const auto&n=nodes[index];error="invalid hierarchy node";
   if(!n.objectId||!n.transformId||!ids.insert(n.objectId).second||n.parent<-1||(n.parent>=0&&size_t(n.parent)>=nodes.size())||n.parent==int32_t(index)||n.flags&~3u||n.layer>=32||n.tag>65535||n.firstComponent!=nextComponent||!n.componentCount||uint64_t(n.firstComponent)+n.componentCount>components.size()||!stringRange(n.nameOffset,n.nameLength))return false;
   for(float v:{n.position.x,n.position.y,n.position.z,n.scale.x,n.scale.y,n.scale.z,n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]})if(!std::isfinite(v))return false;
   float sq=0;for(float v:n.rotation)sq+=v*v;if(std::abs(sq-1)>.002f)return false;
   nextComponent+=n.componentCount;
   int32_t at=int32_t(index);unsigned depth=0;while(at!=-1){if(++depth>512){error="hierarchy cycle or depth exceeded";return false;}at=nodes[size_t(at)].parent;if(at<-1||(at>=0&&size_t(at)>=nodes.size()))return false;}
  }
  if(nextComponent!=components.size())return false;
  for(size_t index=0;index<nodes.size();index++){
   const auto&n=nodes[index];unsigned transforms=0;
   for(size_t i=n.firstComponent;i<size_t(n.firstComponent)+n.componentCount;i++){
    const auto&c=components[i];error="invalid hierarchy component ownership";
    if(!c.id||!ids.insert(c.id).second||c.node!=index||c.flags&~3u||(!(c.flags&2)&&!(c.flags&1))||!c.typeLength||!stringRange(c.typeOffset,c.typeLength)||!stringRange(c.scriptOffset,c.scriptLength))return false;
    std::string type=text(c.typeOffset,c.typeLength);
    if(type=="Transform"||type=="RectTransform"){if(c.id!=n.transformId||type!=((n.flags&2)?"RectTransform":"Transform"))return false;transforms++;}
   }if(transforms!=1)return false;
  }
  error.clear();return true;
 }
};
}
