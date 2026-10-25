#pragma once
#include <cstdint>
#include <limits>

namespace nxfg {
// Exact std140 layout read by pyramid.glsl (13 uvec4 entries).
struct PyramidLevel { uint32_t offset,width,height,padding; };
struct PyramidLayout {
 PyramidLevel levels[13]{};
 uint32_t count=0,bytes=0;
 bool Initialize(uint32_t width,uint32_t height,uint32_t declaredMips=0){
  *this={};if(!width||!height||width>4096||height>4096)return false;
  unsigned available=1;for(uint32_t n=width>height?width:height;n>1;n>>=1)available++;
  if(!declaredMips)declaredMips=available;
  if(declaredMips>available||declaredMips>13)return false;
  uint64_t pixels=0;
  for(uint32_t mip=0;mip<declaredMips;mip++){
   uint32_t w=width>>mip,h=height>>mip;if(!w)w=1;if(!h)h=1;
   if(pixels+uint64_t(w)*h>std::numeric_limits<uint32_t>::max()/8)return false;
   levels[mip]={uint32_t(pixels),w,h,0};pixels+=uint64_t(w)*h;
  }
  // Unused entries have zero dimensions; GLSL bounds checks ignore writes.
  count=declaredMips;bytes=uint32_t(pixels*8);return true;
 }
};
static_assert(sizeof(PyramidLevel)==16,"std140 uvec4 ABI");
}
