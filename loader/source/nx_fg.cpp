#include <switch.h>
#include <GLES3/gl3.h>
#include <atomic>
#include <memory>
#include <new>
#include <cmath>
#include "nx_fg.h"
#include "nx_fg_interop.h"
#include "nx_fg_capture.h"
#include "nx_fg_budget.h"
#include "../../experimental/framegen/deko/gpu_backend.h"
extern "C" {
#include "nx_gameroot.h"
#include "nx_perf.h"
int debugPrintf(char*,...);
extern volatile u64 g_swap_enter_tick;
}
static_assert(sizeof(NxFgDescription)==36,"managed/native FG description ABI");
static_assert(sizeof(NxFgFrame)==40,"managed/native FG frame ABI");
namespace {
std::atomic<bool> gpuFault{false};
std::atomic<bool> performanceBlocked{false};
std::atomic<int> runtimeState{0};
void gpuLog(const char* text){debugPrintf((char*)"[fg-job] %s\n",text);}
struct Session;
Session* active=nullptr; // Only accessed on the GLES render/present thread.
struct Job {std::atomic<unsigned> state{0};Session* session=nullptr;NxFgFrame frame{};};
struct GlState {
 GLint program,vao,activeTexture,texture,sampler,drawFbo,readFbo,viewport[4];
 GLboolean mask[4];
 const GLenum caps[8]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE};
 GLboolean enabled[8];
 GlState(){
  glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
  glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegeri_v(GL_SAMPLER_BINDING,0,&sampler);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_VIEWPORT,viewport);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
  for(unsigned i=0;i<8;i++){enabled[i]=glIsEnabled(caps[i]);glDisable(caps[i]);}
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
 }
 ~GlState(){
  for(unsigned i=0;i<8;i++){if(enabled[i])glEnable(caps[i]);else glDisable(caps[i]);}
  glColorMask(mask[0],mask[1],mask[2],mask[3]);glUseProgram(program);glBindVertexArray(vao);
  glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(0,sampler);glActiveTexture(activeTexture);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
  glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
 }
};
struct Session {
 const NxFgDescription desc;
 std::atomic<int> status{0};Job jobs[8];
 std::unique_ptr<nxfg::GpuBackend> gpu;
 std::unique_ptr<nxfg::SdkBackend> backend;
 std::unique_ptr<FfxOpticalflowContext> optical;
 std::unique_ptr<FfxFrameInterpolationContext> interpolation;
 nxfg::Transfer colorTransfer,depthTransfer,motionTransfer,outputTransfer;
 FfxResource color{},depth{},motion{},output{},vectors{},sceneChange{},dilatedDepth{},dilatedMotion{},previousDepth{};
 FfxOpticalflowSharedResourceDescriptions os{};
 EGLContext context=EGL_NO_CONTEXT;
 GLuint program=0,vao=0,sampler=0;
 bool opticalCreated=false,interpolationCreated=false,readyToPresent=false,history=false,quarantined=false;
 uint64_t previousFrame=0;unsigned frames=0,generatedPresents=0,realPresents=0;
 NxFgCapture capture;unsigned missingCapture=0;
 NxFgBudget budget;
 explicit Session(const NxFgDescription& d):desc(d){for(auto& j:jobs)j.session=this;}
 bool fail(const char* text){
  runtimeState.store(0);
  readyToPresent=false;status.store(-1,std::memory_order_release);if(active==this)active=nullptr;
  if(gpu&&gpu->Quarantined()){
   quarantined=true;gpuFault.store(true);colorTransfer.Quarantine();depthTransfer.Quarantine();motionTransfer.Quarantine();outputTransfer.Quarantine();
  }
  debugPrintf((char*)"[fg] disabled: %s backend=%s pass=%s\n",text,gpu?gpu->Error():"none",gpu?gpu->LastPass():"none");return false;
 }
 bool checked(FfxErrorCode code,const char* text){return code==FFX_OK&&gpu->Ready()?true:fail(text);}
 FfxResource texture(unsigned w,unsigned h,FfxSurfaceFormat format){
  FfxCreateResourceDescription d{};d.resourceDescription={FFX_RESOURCE_TYPE_TEXTURE2D,format,w,h,1,1,FFX_RESOURCE_FLAGS_NONE,FFX_RESOURCE_USAGE_UAV};return gpu->Shared(d);
 }
 bool shaders(){
  const char* vertex="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
  const char* fragment="#version 300 es\nprecision highp float;uniform sampler2D frame;in vec2 uv;out vec4 color;void main(){color=texture(frame,uv);}";
  GLuint stages[2]={glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};
  const char* sources[2]={vertex,fragment};bool valid=true;
  for(unsigned i=0;i<2;i++){glShaderSource(stages[i],1,&sources[i],nullptr);glCompileShader(stages[i]);GLint success;glGetShaderiv(stages[i],GL_COMPILE_STATUS,&success);valid=valid&&success;}
  program=glCreateProgram();glAttachShader(program,stages[0]);glAttachShader(program,stages[1]);glLinkProgram(program);
  GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);for(auto shader:stages)glDeleteShader(shader);
  if(!valid||!linked)return fail("FG presentation shader unavailable");
  glGenVertexArrays(1,&vao);glGenSamplers(1,&sampler);glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glSamplerParameteri(sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  return true;
 }
 bool initialize(){
  context=eglGetCurrentContext();if(context==EGL_NO_CONTEXT)return fail("no current GLES context");
  char directory[768];game_path(directory,sizeof directory,"assets/NxPort/framegen");
  gpu=std::make_unique<nxfg::GpuBackend>(directory,gpuLog);if(!gpu->Ready())return fail("GPU backend initialization");
  backend=std::make_unique<nxfg::SdkBackend>(*gpu);
  optical=std::make_unique<FfxOpticalflowContext>();interpolation=std::make_unique<FfxFrameInterpolationContext>();
  FfxOpticalflowContextDescription od{};od.resolution={desc.outputWidth,desc.outputHeight};od.backendInterface=backend->Interface();
  if(!checked(ffxOpticalflowContextCreate(optical.get(),&od),"OF context creation"))return false;opticalCreated=true;
  FfxFrameInterpolationContextDescription fd{};fd.maxRenderSize={desc.renderWidth,desc.renderHeight};fd.displaySize=od.resolution;fd.backBufferFormat=fd.previousInterpolationSourceFormat=FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;fd.backendInterface=backend->Interface();
  if(!checked(ffxFrameInterpolationContextCreate(interpolation.get(),&fd),"FI context creation"))return false;interpolationCreated=true;
  FfxFrameInterpolationSharedResourceDescriptions fs{};
  if(!checked(ffxOpticalflowGetSharedResourceDescriptions(optical.get(),&os),"OF shared descriptions")||!checked(ffxFrameInterpolationGetSharedResourceDescriptions(interpolation.get(),&fs),"FI shared descriptions"))return false;
  color=texture(desc.outputWidth,desc.outputHeight,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);output=texture(desc.outputWidth,desc.outputHeight,FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
  depth=texture(desc.renderWidth,desc.renderHeight,FFX_SURFACE_FORMAT_R32_FLOAT);motion=texture(desc.renderWidth,desc.renderHeight,FFX_SURFACE_FORMAT_R16G16_FLOAT);
  vectors=gpu->Shared(os.opticalFlowVector);sceneChange=gpu->Shared(os.opticalFlowSCD);dilatedDepth=gpu->Shared(fs.dilatedDepth);dilatedMotion=gpu->Shared(fs.dilatedMotionVectors);previousDepth=gpu->Shared(fs.reconstructedPrevNearestDepth);
  if(!color.resource||!output.resource||!depth.resource||!motion.resource||!vectors.resource||!sceneChange.resource||!dilatedDepth.resource||!dilatedMotion.resource||!previousDepth.resource)return fail("FG shared resources");
  if(!colorTransfer.Initialize(gpu->Device(),desc.outputWidth,desc.outputHeight,GL_RGBA,GL_UNSIGNED_BYTE,4)||!depthTransfer.Initialize(gpu->Device(),desc.renderWidth,desc.renderHeight,GL_RGBA,GL_UNSIGNED_BYTE,4)||!motionTransfer.Initialize(gpu->Device(),desc.renderWidth,desc.renderHeight,GL_RGBA,GL_UNSIGNED_BYTE,4)||!outputTransfer.Initialize(gpu->Device(),desc.outputWidth,desc.outputHeight,GL_RGBA,GL_UNSIGNED_BYTE,4))return fail("GLES/deko shared PBO initialization");
  if(!shaders())return false;
  debugPrintf((char*)"[fg] native AMD 3.1 graph ready: render=%ux%u output=%ux%u; HUD included\n",desc.renderWidth,desc.renderHeight,desc.outputWidth,desc.outputHeight);return true;
 }
 void render(const NxFgFrame& frame){
  readyToPresent=false;if(status.load()<0||gpuFault.load())return;
  if(performanceBlocked.load()){fail("FG performance budget exceeded; normal rendering retained");return;}
  if(!capture.Consume()){
   if(missingCapture++%300==0)debugPrintf((char*)"[fg] waiting for world capture marker; skipped=%u\n",missingCapture);
   return;
  }
  if(!gpu&&!initialize())return;
  // Profile just the first history frame, then restore normal batching. These
  // timings include extra serialization and are labelled as diagnostic.
  gpu->TraceJobs(frames==1);
  if(context!=eglGetCurrentContext()){fail("render context changed");return;}
  bool reset=frame.reset||!history||frame.frameId!=previousFrame+1;
  u64 start=armGetSystemTick();
  auto elapsed=[](u64 a,u64 b){return double(b-a)*1000/19200000.0;};
  if(!colorTransfer.Capture(desc.color)||!depthTransfer.Capture(desc.depth)||!motionTransfer.Capture(desc.motion)||!outputTransfer.WaitConsumer()){
   quarantined=true;gpuFault.store(true);colorTransfer.Quarantine();depthTransfer.Quarantine();motionTransfer.Quarantine();outputTransfer.Quarantine();fail("shared PBO producer/consumer fence");return;
  }
  u64 captureEnd=armGetSystemTick();
  // All three independent uploads precede OF/FI. Submit once rather than
  // blocking the render thread on three separate queue flushes and fences.
  if(!checked(gpu->Upload(color,colorTransfer.Address(),false),"color upload")||!checked(gpu->Upload(depth,depthTransfer.Address(),false),"depth upload")||!checked(gpu->Upload(motion,motionTransfer.Address()),"motion upload"))return;
  u64 uploadEnd=armGetSystemTick();
  FfxOpticalflowDispatchDescription of{};of.commandList=this;of.color=color;of.opticalFlowVector=vectors;of.opticalFlowSCD=sceneChange;of.reset=reset;of.minMaxLuminance={0,1};
  if(!checked(ffxOpticalflowContextDispatch(optical.get(),&of),"OF dispatch"))return;
  u64 opticalEnd=armGetSystemTick();
  FfxFrameInterpolationPrepareDescription p{};p.commandList=this;p.renderSize={desc.renderWidth,desc.renderHeight};p.motionVectorScale={-float(desc.renderWidth),-float(desc.renderHeight)};p.jitterOffset={frame.jitterX,frame.jitterY};p.frameTimeDelta=frame.milliseconds;p.cameraNear=frame.nearPlane;p.cameraFar=frame.farPlane;p.cameraFovAngleVertical=frame.fovRadians;p.viewSpaceToMetersFactor=1;p.depth=depth;p.motionVectors=motion;p.frameID=frame.frameId;p.dilatedDepth=dilatedDepth;p.dilatedMotionVectors=dilatedMotion;p.reconstructedPrevDepth=previousDepth;
  if(!checked(ffxFrameInterpolationPrepare(interpolation.get(),&p),"FI prepare"))return;
  u64 prepareEnd=armGetSystemTick();
  FfxFrameInterpolationDispatchDescription f{};f.commandList=this;f.displaySize=of.color.description.width?FfxDimensions2D{desc.outputWidth,desc.outputHeight}:FfxDimensions2D{};f.renderSize=p.renderSize;f.currentBackBuffer=color;f.output=output;f.interpolationRect={0,0,int32_t(desc.outputWidth),int32_t(desc.outputHeight)};f.opticalFlowVector=vectors;f.opticalFlowSceneChangeDetection=sceneChange;f.opticalFlowBufferSize={os.opticalFlowVector.resourceDescription.width,os.opticalFlowVector.resourceDescription.height};f.opticalFlowScale={1.0f/desc.outputWidth,1.0f/desc.outputHeight};f.opticalFlowBlockSize=8;f.cameraNear=p.cameraNear;f.cameraFar=p.cameraFar;f.cameraFovAngleVertical=p.cameraFovAngleVertical;f.viewSpaceToMetersFactor=1;f.frameTimeDelta=p.frameTimeDelta;f.reset=reset;f.minMaxLuminance[1]=1;f.frameID=frame.frameId;f.dilatedDepth=dilatedDepth;f.dilatedMotionVectors=dilatedMotion;f.reconstructedPrevDepth=previousDepth;
  if(!checked(ffxFrameInterpolationDispatch(interpolation.get(),&f),"FI dispatch"))return;
  u64 interpolationEnd=armGetSystemTick();
  if(!checked(gpu->Download(output,outputTransfer.Address()),"interpolated output download"))return;
  if(!outputTransfer.Upload(desc.generated)){quarantined=true;gpuFault.store(true);outputTransfer.Quarantine();fail("generated texture upload");return;}
  double milliseconds=double(armGetSystemTick()-start)*1000/19200000.0;
  if(frames<3||frames%300==0)debugPrintf((char*)"[fg-profile] capture=%.2f upload=%.2f optical=%.2f prepare=%.2f interpolation=%.2f output=%.2f ms; synchronized wall times\n",elapsed(start,captureEnd),elapsed(captureEnd,uploadEnd),elapsed(uploadEnd,opticalEnd),elapsed(opticalEnd,prepareEnd),elapsed(prepareEnd,interpolationEnd),elapsed(interpolationEnd,armGetSystemTick()));
  if(budget.Observe(milliseconds,reset)){
   performanceBlocked.store(true);
   debugPrintf((char*)"[fg] automatic fallback: %.2f ms, two consecutive frames over 16.67ms budget; toggle FG off/on to retry\n",milliseconds);
   fail("frame generation is slowing the game");return;
  }
  history=true;previousFrame=frame.frameId;readyToPresent=!reset;active=this;status.store(1,std::memory_order_release);
  runtimeState.store(1);
  if(frames++<3||frames%300==0)debugPrintf((char*)"[fg] completed frame=%llu reset=%d compute+transfer=%.2f ms present_extra=%d\n",(unsigned long long)frame.frameId,int(reset),double(armGetSystemTick()-start)*1000/19200000.0,int(readyToPresent));
 }
 void draw(GLuint texture){
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(0,0,desc.outputWidth,desc.outputHeight);glUseProgram(program);glBindVertexArray(vao);glBindSampler(0,sampler);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(glGetUniformLocation(program,"frame"),0);glDrawArrays(GL_TRIANGLES,0,3);
 }
 ~Session(){
  debugPrintf((char*)"[fg-present] session summary: completed=%u generated_swaps=%u real_swaps=%u\n",frames,generatedPresents,realPresents);
  if(active==this){active=nullptr;runtimeState.store(0);}
  if(interpolationCreated)ffxFrameInterpolationContextDestroy(interpolation.get());
  if(opticalCreated)ffxOpticalflowContextDestroy(optical.get());
  if(program)glDeleteProgram(program);if(vao)glDeleteVertexArrays(1,&vao);if(sampler)glDeleteSamplers(1,&sampler);
 }
};
void callback(int event,void* data){
 if(event==13){auto s=(Session*)data;if(s){s->capture.Mark();if(s->capture.produced==1)debugPrintf((char*)"[fg] first world depth/motion capture marker reached render thread\n");}}
 else if(event==11){auto job=(Job*)data;if(!job||job->state.load(std::memory_order_acquire)!=1)return;job->session->render(job->frame);job->state.store(0,std::memory_order_release);}
 else if(event==12){auto s=(Session*)data;if(!s)return;if(active==s){active=nullptr;runtimeState.store(0);}
  // Finish ALL GLES work queued before release, including Unity's capture
  // blits on frames with no interpolation job. The output-consumer fence alone
  // only covers the last glTexSubImage2D, not those subsequent commands.
  GLsync releaseFence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
  GLenum releaseResult=releaseFence?glClientWaitSync(releaseFence,GL_SYNC_FLUSH_COMMANDS_BIT,2000000000ULL):GL_WAIT_FAILED;
  if(releaseFence)glDeleteSync(releaseFence);
  if(releaseResult!=GL_ALREADY_SIGNALED&&releaseResult!=GL_CONDITION_SATISFIED){
   s->quarantined=true;s->colorTransfer.Quarantine();s->depthTransfer.Quarantine();s->motionTransfer.Quarantine();s->outputTransfer.Quarantine();
   debugPrintf((char*)"[fg] release fence failed; retaining complete session\n");
  }
  if(s->outputTransfer.Ready()&&!s->outputTransfer.WaitConsumer()){
   s->quarantined=true;s->outputTransfer.Quarantine();
  }
  if(s->quarantined||(s->gpu&&s->gpu->Quarantined())){gpuFault.store(true);return;}
  delete s;
 }
}
}
extern "C" void* nxb_fg_create(const NxFgDescription* d){
 if(performanceBlocked.load())return nullptr;
 if(!d||d->version!=1||!d->color||!d->depth||!d->motion||!d->generated||!d->renderWidth||!d->renderHeight||!d->outputWidth||!d->outputHeight||d->renderWidth>1920||d->renderHeight>1080||d->outputWidth>1920||d->outputHeight>1080||gpuFault.load())return nullptr;
 return new(std::nothrow)Session(*d);
}
extern "C" void* nxb_fg_enqueue(void* ptr,const NxFgFrame* f){
 auto s=(Session*)ptr;if(!s||!f||s->status.load()<0)return nullptr;
 if(!std::isfinite(f->milliseconds)||f->milliseconds<=0||!std::isfinite(f->nearPlane)||!std::isfinite(f->farPlane)||!std::isfinite(f->fovRadians)||f->nearPlane<=0||f->farPlane<=f->nearPlane||f->fovRadians<=0||f->fovRadians>=3.14f){
  static unsigned rejected=0;if(rejected++<3)debugPrintf((char*)"[fg] rejected frame metadata: dt=%f near=%f far=%f fov=%f\n",double(f->milliseconds),double(f->nearPlane),double(f->farPlane),double(f->fovRadians));
  return nullptr;
 }
 for(auto& j:s->jobs){unsigned expected=0;if(j.state.compare_exchange_strong(expected,2)){j.frame=*f;j.state.store(1,std::memory_order_release);return &j;}}
 return nullptr;
}
extern "C" void* nxb_fg_callback(){return (void*)&callback;}
extern "C" int nxb_fg_status(void* ptr){return ptr?((Session*)ptr)->status.load(std::memory_order_acquire):-1;}
extern "C" int nxb_fg_available(void){return !gpuFault.load()&&!performanceBlocked.load();}
extern "C" int nx_fg_runtime_state(void){return gpuFault.load()?-1:performanceBlocked.load()?-2:runtimeState.load();}
extern "C" void nx_fg_settings_changed(void){performanceBlocked.store(false);}
extern "C" EGLBoolean nx_fg_present(EGLDisplay display,EGLSurface surface,int* handled){
 *handled=0;auto s=active;if(!s||!s->readyToPresent||s->status.load()!=1||s->context!=eglGetCurrentContext())return EGL_FALSE;
 s->readyToPresent=false;
 {GlState saved;s->draw(s->desc.generated);u64 begin=armGetSystemTick();EGLBoolean first=eglSwapBuffers(display,surface);
  if(!first){s->draw(s->desc.color);s->fail("interpolated present failed");return EGL_FALSE;}
  ++s->generatedPresents;
  runtimeState.store(2);
  nx_perf_present(begin,armGetSystemTick(),1);g_swap_enter_tick=armGetSystemTick();
  s->draw(s->desc.color);EGLBoolean second=eglSwapBuffers(display,surface);*handled=1;
  if(second)++s->realPresents;
  if(s->generatedPresents<=3||s->generatedPresents%300==0)debugPrintf((char*)"[fg-present] successful generated_swaps=%u real_swaps=%u\n",s->generatedPresents,s->realPresents);
  if(!second)s->fail("real present failed");return second;
 }
}
