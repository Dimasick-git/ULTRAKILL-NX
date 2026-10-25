#include "nx_fg_interop.h"
#include <EGL/egl.h>
#include <cstring>
#include <limits>

namespace nxfg {
namespace {
constexpr uint32_t Alignment=DK_MEMBLOCK_ALIGNMENT;
constexpr GLuint64 TimeoutNs=2000000000ULL;
struct PixelStore {
 GLenum target;
 GLint binding,alignment,rowLength,skipRows,skipPixels;
 explicit PixelStore(bool pack):target(pack?GL_PIXEL_PACK_BUFFER:GL_PIXEL_UNPACK_BUFFER) {
  glGetIntegerv(pack?GL_PIXEL_PACK_BUFFER_BINDING:GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
  glGetIntegerv(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,&alignment);
  glGetIntegerv(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,&rowLength);
  glGetIntegerv(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,&skipRows);
  glGetIntegerv(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,&skipPixels);
  glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,1);
  glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,0);
  glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,0);
  glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,0);
 }
 ~PixelStore(){
  bool pack=target==GL_PIXEL_PACK_BUFFER;
  glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,alignment);
  glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,rowLength);
  glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,skipRows);
  glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,skipPixels);
  glBindBuffer(target,binding);
 }
};
bool persistentBuffers(){
 GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
 for(GLint i=0;i<count;i++){
  const char* ext=(const char*)glGetStringi(GL_EXTENSIONS,i);
  if(ext&&!std::strcmp(ext,"GL_EXT_buffer_storage"))return true;
 }
 return false;
}
}
bool Transfer::fault(const char* message){error_=message;fault_=true;return false;}
bool Transfer::waitGl(GLsync& sync){
 if(!sync)return true;
 GLenum result=glClientWaitSync(sync,GL_SYNC_FLUSH_COMMANDS_BIT,TimeoutNs);
 if(result!=GL_ALREADY_SIGNALED&&result!=GL_CONDITION_SATISFIED)
  return fault(result==GL_TIMEOUT_EXPIRED?"GLES fence timeout":"GLES fence failed");
 glDeleteSync(sync);sync=nullptr;return true;
}
bool Transfer::WaitConsumer(){return Ready()&&waitGl(glRead_);}
bool Transfer::Initialize(DkDevice device,uint32_t width,uint32_t height,
                          GLenum format,GLenum type,uint32_t bpp){
 if(pbo_||memory_)return fault("transfer already initialized");
 if(!device||!width||!height||width>4096||height>4096||!bpp||bpp>16)
  return fault("invalid transfer dimensions");
 if(!((format==GL_RGBA&&type==GL_UNSIGNED_BYTE&&bpp==4)||
      (format==GL_RED&&type==GL_UNSIGNED_BYTE&&bpp==1)))
  return fault("transfer format not yet supported");
 const uint64_t payload=uint64_t(width)*height*bpp;
 const uint64_t rounded=(payload+Alignment-1)&~uint64_t(Alignment-1);
 if(rounded+Alignment>std::numeric_limits<uint32_t>::max())return fault("transfer size overflow");
 if(!persistentBuffers())return fault("GL_EXT_buffer_storage unavailable");
 auto storage=(PFNGLBUFFERSTORAGEEXTPROC)eglGetProcAddress("glBufferStorageEXT");
 if(!storage)return fault("glBufferStorageEXT unavailable");
 width_=width;height_=height;bpp_=bpp;format_=format;type_=type;
 PixelStore saved(true);glGenBuffers(1,&pbo_);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo_);
 const GLbitfield flags=GL_MAP_READ_BIT|GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT_EXT|GL_MAP_COHERENT_BIT_EXT;
 const GLsizeiptr allocation=rounded+Alignment;
 storage(GL_PIXEL_PACK_BUFFER,allocation,nullptr,flags);
 GLint actual=0;glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_SIZE,&actual);
 if(actual!=allocation)return fault("PBO storage allocation failed");
 mapped_=glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,allocation,flags);
 if(!mapped_)return fault("persistent PBO map failed");
 uintptr_t aligned=(uintptr_t(mapped_)+Alignment-1)&~uintptr_t(Alignment-1);
 offset_=aligned-uintptr_t(mapped_);
 DkMemBlockMaker maker;dkMemBlockMakerDefaults(&maker,device,uint32_t(rounded));
 // No ZeroFillInit: these pages belong to GL, and no CPU pixel writes occur.
 maker.storage=(void*)aligned;maker.flags=DkMemBlockFlags_CpuCached|DkMemBlockFlags_GpuCached;
 memory_=dkMemBlockCreate(&maker);
 if(!memory_)return fault("deko cannot register persistent PBO backing");
 glGenFramebuffers(1,&fbo_);error_="ready";return true;
}
bool Transfer::Capture(GLuint texture){
 if(!Ready()||!texture)return false;
 if(!WaitConsumer())return false;
 PixelStore saved(true);GLint oldFbo;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldFbo);
 glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo_);
 glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
 bool complete=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
 GLsync ready=nullptr;
 if(complete){
  glReadBuffer(GL_COLOR_ATTACHMENT0);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo_);
  glReadPixels(0,0,width_,height_,format_,type_,(void*)offset_);
  ready=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
 }
 // Do not retain a reference to a Unity texture across scene teardown.
 glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
 glBindFramebuffer(GL_READ_FRAMEBUFFER,oldFbo);
 if(!complete)return fault("capture framebuffer incomplete");
 if(!ready)return fault("capture fence allocation failed");
 return waitGl(ready);
}
bool Transfer::Upload(GLuint texture){
 if(!Ready()||!texture||!WaitConsumer())return false;
 PixelStore saved(false);GLint oldTexture;glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
 glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo_);glBindTexture(GL_TEXTURE_2D,texture);
 glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width_,height_,format_,type_,(void*)offset_);
 glRead_=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
 glBindTexture(GL_TEXTURE_2D,oldTexture);
 if(!glRead_)return fault("upload fence allocation failed");
 return true;
}
Transfer::~Transfer(){
 // A failed producer/consumer fence means ownership is unknown. Leaking one
 // bounded transfer until process teardown is safer than GPU use-after-free.
 if(fault_&&memory_)return;
 if(!waitGl(glRead_))return;
 if(memory_)dkMemBlockDestroy(memory_);
 if(pbo_){PixelStore saved(true);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo_);if(mapped_)glUnmapBuffer(GL_PIXEL_PACK_BUFFER);glDeleteBuffers(1,&pbo_);}
 if(fbo_)glDeleteFramebuffers(1,&fbo_);
}
}
