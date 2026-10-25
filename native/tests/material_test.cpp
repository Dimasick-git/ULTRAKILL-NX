#include "nx_material.h"
#include <cstdio>
#include <cstdlib>
using namespace nx;using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
MaterialPack fixture(){
 MaterialPack p;std::memcpy(p.header.magic,"NXMAT\0\0\0",8);p.header.version=1;p.header.materialCount=1;p.header.textureCount=1;p.header.pixelBytes=16;
 MaterialRecord m{};m.file=5;m.id=-8740404655613328582LL;m.shader=1;m.mainTexture=0;m.emissiveTexture=MaterialPack::NoTexture;m.uv[0]=m.uv[1]=1;m.emissiveUv[0]=m.emissiveUv[1]=1;for(auto&v:m.tint)v=1;m.parameters[0]=.5f;p.materials.push_back(m);
 TextureRecord t{};t.width=t.height=2;t.bytes=16;t.sourceId=1234;t.sourceFileCrc=123;p.textures.push_back(t);p.pixels={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};return p;
}
void fixtures(){
 std::string error;auto p=fixture();CHECK(p.validate(error));auto bad=p;bad.materials[0].mainTexture=1;CHECK(!bad.validate(error));bad=p;bad.materials[0].emissiveTexture=1;CHECK(!bad.validate(error));bad=p;bad.textures[0].offset=1;CHECK(!bad.validate(error));bad=p;bad.textures[0].width=UINT32_MAX;CHECK(!bad.validate(error));bad=p;bad.materials[0].uv[0]=NAN;CHECK(!bad.validate(error));bad=p;bad.materials.push_back(bad.materials[0]);bad.header.materialCount++;CHECK(!bad.validate(error));bad=p;bad.textures[0].wrapU=4;CHECK(!bad.validate(error));bad=p;bad.textures[0].reserved[3]=1;CHECK(!bad.validate(error));
 CHECK(p.pixels[0]==255&&p.pixels[4]==0&&p.pixels[5]==255&&p.pixels[10]==255);
 std::puts("PASS: native material identity, texture ranges/dimensions, finite shader parameters and sampler validation");
}
void corruption(const char*path){
 FILE*f=std::fopen(path,"rb");CHECK(f);CHECK(!std::fseek(f,0,SEEK_END));long size=std::ftell(f);CHECK(size>32);std::rewind(f);std::vector<uint8_t> bytes(size);CHECK(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());std::fclose(f);
 auto reject=[](const std::vector<uint8_t>&data){FILE*f=std::tmpfile();CHECK(f);CHECK(std::fwrite(data.data(),1,data.size(),f)==data.size());std::rewind(f);MaterialPack p;std::string error;CHECK(!p.read(f,error));std::fclose(f);};
 auto refresh=[](std::vector<uint8_t>&b){reinterpret_cast<MaterialHeader*>(b.data())->crc=crc32(b.data()+32,b.size()-32);};
 auto bad=bytes;bad.pop_back();reject(bad);bad=bytes;bad.push_back(0);reject(bad);bad=bytes;reinterpret_cast<MaterialHeader*>(bad.data())->pixelBytes=UINT32_MAX;reject(bad);
 for(unsigned i=0;i<12;i++){bad=bytes;bad[32+(i*104729)%(bad.size()-32)]^=1;reject(bad);}
 bad=bytes;reinterpret_cast<MaterialRecord*>(bad.data()+32)->mainTexture=65536;refresh(bad);reject(bad);bad=bytes;reinterpret_cast<MaterialRecord*>(bad.data()+32)->parameters[1]=INFINITY;refresh(bad);reject(bad);
 std::puts("PASS: material pack exact length, allocation budgets, checksum mutations and refreshed-checksum invalid parameters");
}
int main(int argc,char**argv){
 fixtures();if(argc!=3){std::fprintf(stderr,"usage: nx_material_test MATERIAL_PACK RENDER_PACK\n");return 2;}
 std::string error;MaterialPack p;RenderPack r;CHECK(p.load(argv[1],error));CHECK(r.load(argv[2],error));std::vector<uint32_t> bindings;CHECK(p.bind(r,bindings,error));CHECK(bindings.size()==r.chunks.size());
 for(size_t i=0;i<bindings.size();i++){const auto&m=p.materials[bindings[i]];CHECK(m.file==r.chunks[i].materialFile&&m.id==r.chunks[i].materialId);}
 auto bad=p;bad.header.renderCrc^=1;std::vector<uint32_t> untouched={42};CHECK(!bad.bind(r,untouched,error));CHECK(untouched.size()==1&&untouched[0]==42);bad=p;bad.materials.pop_back();bad.header.materialCount--;CHECK(!bad.bind(r,untouched,error));CHECK(untouched[0]==42);
 CHECK(!p.load("missing-material-file-for-transaction-check",error));CHECK(p.bind(r,bindings,error));
 std::printf("PASS: actual material pack: %zu original materials, %zu textures, %zu pixel bytes; every one of %zu chunks resolves its exact original material; shader/GPU parity unverified\n",p.materials.size(),p.textures.size(),p.pixels.size(),bindings.size());corruption(argv[1]);return 0;
}
