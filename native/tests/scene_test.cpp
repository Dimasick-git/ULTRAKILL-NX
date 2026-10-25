#include "nx_scene.h"
#include <cassert>
#include <string>
#include <random>

static bool decode(const std::vector<uint8_t>&bytes){
 FILE*f=std::tmpfile();assert(f);assert(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());std::rewind(f);
 nx::Scene scene;const char*error=nullptr;bool ok=scene.read(f,&error);std::fclose(f);return ok;
}
static void updateCrc(std::vector<uint8_t>&bytes){auto*header=(nx::Header*)bytes.data();header->payloadCrc=nx::crc32(bytes.data()+sizeof(nx::Header),bytes.size()-sizeof(nx::Header));}
int main(int argc,char**argv){
 assert(argc==2);nx::Scene scene;const char*error=nullptr;if(!scene.load(argv[1],&error)){std::fprintf(stderr,"scene load failed: %s\n",error);return 1;}
 auto camera=scene.camera();auto matrix=camera.matrix(1280.f/720);unsigned visible=0;for(auto&chunk:scene.chunks)visible+=nx::visible(chunk,matrix);
 assert(visible>0 && visible<=scene.chunks.size());
 nx::Camera test;test.position={0,0,0};auto m=test.matrix(1);
 nx::Chunk box{0,3,{-1,-1,4},{1,1,6}};assert(nx::visible(box,m));
 box={{0},3,{-1,-1,-6},{1,1,-4}};assert(!nx::visible(box,m));
 box={0,3,{100,-1,4},{102,1,6}};assert(!nx::visible(box,m));
 box={0,3,{-1,-1,.01f},{1,1,.1f}};assert(nx::visible(box,m));
 box={0,3,{-1,-1,3000},{1,1,3010}};assert(!nx::visible(box,m));
 test.yaw=3.14159265359f;box={0,3,{-1,-1,-6},{1,1,-4}};assert(nx::visible(box,test.matrix(1)));
 FILE*f=std::fopen(argv[1],"rb");assert(f);std::fseek(f,0,SEEK_END);auto length=std::ftell(f);std::rewind(f);std::vector<uint8_t>original(length);assert(std::fread(original.data(),1,original.size(),f)==original.size());std::fclose(f);
 assert(decode(original));
 auto bad=original;bad.pop_back();assert(!decode(bad));bad=original;bad.push_back(0);assert(!decode(bad));
 bad=original;((nx::Header*)bad.data())->vertexCount=0xffffffff;assert(!decode(bad));
 bad=original;((nx::Header*)bad.data())->spawn[0]=NAN;assert(!decode(bad));
 bad=original;auto indexOffset=sizeof(nx::Header)+scene.vertices.size()*sizeof(nx::Vertex);*((uint32_t*)(bad.data()+indexOffset))=scene.vertices.size();updateCrc(bad);assert(!decode(bad));
 bad=original;auto chunkOffset=indexOffset+scene.indices.size()*4;((nx::Chunk*)(bad.data()+chunkOffset))->minimum[0]=1e9;updateCrc(bad);assert(!decode(bad));
 bad=original;((nx::Chunk*)(bad.data()+chunkOffset))->firstIndex=1;updateCrc(bad);assert(!decode(bad));
 bad=original;((nx::Vertex*)(bad.data()+sizeof(nx::Header)))->position[0]=NAN;updateCrc(bad);assert(!decode(bad));
 std::mt19937 random(1);for(int i=0;i<100;i++){bad=original;size_t at=sizeof(nx::Header)+random()%(bad.size()-sizeof(nx::Header));bad[at]^=1u<<(random()%8);assert(!decode(bad));}
 std::printf("PASS: actual scene %zu vertices, %zu triangles, %zu chunks (%u initially visible); camera/clip planes, CRC, budgets, truncation, indices/bounds/NaN and 100 payload mutations\n",scene.vertices.size(),scene.indices.size()/3,scene.chunks.size(),visible);
}
