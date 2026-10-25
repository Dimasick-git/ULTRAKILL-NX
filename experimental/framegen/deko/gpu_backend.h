#pragma once
#include "sdk_backend.h"
#include "pyramid_layout.h"
#include "owned_memory.h"
#include <deko3d.h>
#include <string>
#include <map>

namespace nxfg {
// Synchronous bring-up executor. It runs the real SDK jobs; presentation and
// asynchronous pacing are separate. Never reuse its storage after a GPU fault.
class GpuBackend final:public SdkSink {
 struct Resource {
  FfxResourceDescription desc{};unsigned owner=0;bool alive=false;
  DkMemBlock memory=nullptr,packed=nullptr;DkImage image{};PyramidLayout pyramid;
 };
 struct Program {DkShader shader{};DkMemBlock code=nullptr;};
 std::deque<Resource> resources_;
 std::map<std::string,Program> programs_;
 DkDevice device_=nullptr;DkQueue queue_=nullptr;DkCmdBuf commands_=nullptr;
 DkMemBlock commandMemory_=nullptr,scratch_=nullptr,samplers_=nullptr;
 unsigned scratchOffset_=0;
 bool failed_=false,quarantined_=false;
 const char* error_="not initialized";
 std::string shaderRoot_;
 void (*logger_)(const char*)=nullptr;
 bool traceJobs_=false;
 OwnedMemory ownedMemory_;
 const char* lastPass_="none";
 DkMemBlock allocate(uint32_t,uint32_t extra=0);
 FfxErrorCode fail(const char*);
 Resource* resource(FfxResourceInternal);
 FfxErrorCode dispatch(const FfxComputeJobDescription&);
 FfxErrorCode clear(const FfxClearFloatJobDescription&);
 FfxErrorCode copy(const FfxCopyJobDescription&);
 DkGpuAddr stage(const void*,unsigned,unsigned alignment=256);
public:
 explicit GpuBackend(const char* shaderRoot,void (*logger)(const char*)=nullptr,bool traceJobs=false);
 ~GpuBackend();
 bool Ready()const{return !failed_&&device_&&queue_&&commands_;}
 bool Quarantined()const{return quarantined_;}
 const char* Error()const{return error_;}
 const char* LastPass()const{return lastPass_;}
 void TraceJobs(bool enabled){traceJobs_=enabled;}
 DkDevice Device()const{return device_;}
 FfxErrorCode Create(const FfxCreateResourceDescription&,unsigned,FfxResourceInternal&)override;
 FfxErrorCode Destroy(FfxResourceInternal,unsigned)override;
 FfxErrorCode Register(const FfxResource&,unsigned,FfxResourceInternal&)override;
 FfxResource External(FfxResourceInternal)override;
 FfxResourceDescription Describe(FfxResourceInternal)override;
 FfxErrorCode Unregister(unsigned)override{return FFX_OK;}
 FfxErrorCode Pipeline(const NxFfxPass&,const FfxPipelineDescription&,unsigned)override;
 FfxErrorCode Schedule(const FfxGpuJobDescription&)override;
 FfxErrorCode Execute(FfxCommandList,unsigned)override;
 FfxErrorCode ReleaseContext(unsigned)override;
 FfxResource Shared(const FfxCreateResourceDescription&);
 FfxErrorCode Upload(FfxResource,DkGpuAddr,bool submit=true);
 FfxErrorCode Download(FfxResource,DkGpuAddr);
};
}
