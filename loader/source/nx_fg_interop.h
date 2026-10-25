#pragma once
#include <deko3d.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <cstdint>

namespace nxfg {
// GLES PBO and deko NvMap refer to the SAME backing pages. Mapping supplies
// the address for registration only: Capture/Upload never touch pixel bytes.
// All methods, including destruction, require the owning GLES render context.
class Transfer {
 GLuint pbo_=0, fbo_=0;
 void* mapped_=nullptr;
 GLintptr offset_=0;
 DkMemBlock memory_=nullptr;
 GLsync glRead_=nullptr;
 uint32_t width_=0,height_=0,bpp_=0;
 GLenum format_=0,type_=0;
 bool fault_=false;
 const char* error_="not initialized";
 bool waitGl(GLsync& sync);
 bool fault(const char* message);
public:
 Transfer()=default;
 Transfer(const Transfer&)=delete;
 Transfer& operator=(const Transfer&)=delete;
 ~Transfer();
 bool Initialize(DkDevice device,uint32_t width,uint32_t height,
                 GLenum format,GLenum type,uint32_t bytesPerPixel);
 bool Capture(GLuint texture);
 // Caller must have waited the producer deko fence successfully before Upload.
 bool Upload(GLuint texture);
 // Call before deko writes this buffer again: finishes the last GLES consumer.
 bool WaitConsumer();
 // A failed GPU fence makes shared memory unsafe to recycle. The owner must
// stop this backend; keep its pages alive until process/device teardown.
 void Quarantine(){fault_=true;}
 bool Ready()const{return memory_&&!fault_;}
 DkGpuAddr Address()const{return memory_?dkMemBlockGetGpuAddr(memory_):DK_GPU_ADDR_INVALID;}
 uint32_t Width()const{return width_;}
 uint32_t Height()const{return height_;}
 uint32_t Pitch()const{return width_*bpp_;}
 uint32_t LayerStride()const{return Pitch()*height_;}
 const char* Error()const{return error_;}
};
}
