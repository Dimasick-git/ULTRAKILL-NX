#pragma once
#include "host_compat.h"
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include "bindings_sdr.h"
#include <deque>
#include <vector>

namespace nxfg {
// The SDK owns scheduling/history. A Sink owns actual deko resources and must
// copy constants/record commands during Schedule, while borrowed job data lives.
// Execute commits recorded GPU work and returns before the CPU scratch is reused.
class SdkSink {
public:
 virtual ~SdkSink()=default;
 virtual FfxErrorCode Create(const FfxCreateResourceDescription&,unsigned,FfxResourceInternal&)=0;
 virtual FfxErrorCode Destroy(FfxResourceInternal,unsigned)=0;
 virtual FfxErrorCode Register(const FfxResource&,unsigned,FfxResourceInternal&)=0;
 virtual FfxResource External(FfxResourceInternal)=0;
 virtual FfxResourceDescription Describe(FfxResourceInternal)=0;
 virtual FfxErrorCode Unregister(unsigned)=0;
 virtual FfxErrorCode Pipeline(const NxFfxPass&,const FfxPipelineDescription&,unsigned)=0;
 virtual FfxErrorCode Schedule(const FfxGpuJobDescription&)=0;
 virtual FfxErrorCode Execute(FfxCommandList,unsigned)=0;
 virtual FfxErrorCode ReleaseContext(unsigned)=0;
};
class SdkBackend {
 SdkSink& sink_;
 FfxInterface interface_{};
 unsigned nextContext_=1;
 std::deque<std::vector<uint32_t>> constants_;
 static SdkBackend& get(FfxInterface* i){return *(SdkBackend*)i->scratchBuffer;}
 static FfxErrorCode pipeline(FfxInterface*,FfxEffect,FfxPass,uint32_t,const FfxPipelineDescription*,FfxUInt32,FfxPipelineState*);
public:
 explicit SdkBackend(SdkSink& sink);
 FfxInterface Interface()const{return interface_;}
};
}
