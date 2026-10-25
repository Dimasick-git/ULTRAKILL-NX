#include "nx_scene.h"
#include "nx_clock.h"
#include "nx_session.h"
#include "nx_render.h"
#include "nx_gpu_material.h"
#include <switch.h>
#include <deko3d.h>
#include <cstdarg>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <cstddef>

namespace {
FILE* logfile;
void report(const char*fmt,...){if(!logfile)return;va_list args;va_start(args,fmt);std::vfprintf(logfile,fmt,args);va_end(args);std::fflush(logfile);}
[[noreturn]] void fail(const char*message){report("FAIL: %s\n",message);std::exit(EXIT_FAILURE);}
void check(bool value,const char*message){if(!value)fail(message);}
void checkSession(bool value,const std::string&message){if(!value)fail(message.c_str());}
void gpuMessage(void*,const char*where,DkResult result,const char*message){report("[deko] %s rc=%d %s\n",where,int(result),message);if(result!=DkResult_Success)fail("deko validation");}
uint32_t alignUp(uint32_t value,uint32_t alignment){return (value+alignment-1)&~(alignment-1);}

class Renderer {
 static constexpr unsigned Width=1280,Height=720,Frames=2,CommandBytes=4*1024*1024;
 DkDevice device{};DkQueue queue{};DkSwapchain swapchain{};
 DkMemBlock colorMemory{},depthMemory{},codeMemory{},geometryMemory{},uniformMemory{},readbackMemory{};
 DkImage colors[Frames]{},depth{};DkShader vertex{},fragment{};
 struct Frame {DkCmdBuf commands{};DkMemBlock memory{};DkFence fence{};bool submitted=false;} frames[Frames];
 unsigned frameIndex=0;uint32_t codeOffset=0,indexOffset=0;const nx::game::RenderPack& scene;nx::game::RenderPlan plan;
 std::unique_ptr<nx::game::GpuMaterials> materials;
 DkMemBlock allocate(uint32_t size,uint32_t flags){DkMemBlockMaker maker;dkMemBlockMakerDefaults(&maker,device,alignUp(size,DK_MEMBLOCK_ALIGNMENT));maker.flags=flags;auto memory=dkMemBlockCreate(&maker);check(memory,"GPU memory allocation");return memory;}
 void loadShader(DkShader&shader,const char*path,DkStage expected){
  FILE*f=std::fopen(path,"rb");check(f,"shader file missing");check(!std::fseek(f,0,SEEK_END),"shader seek");long length=std::ftell(f);std::rewind(f);
  check(length>=24&&length<=65536&&codeOffset+length<=128*1024,"invalid shader size");
  check(std::fread((uint8_t*)dkMemBlockGetCpuAddr(codeMemory)+codeOffset,1,length,f)==size_t(length),"shader read");std::fclose(f);
  DkShaderMaker maker;dkShaderMakerDefaults(&maker,codeMemory,codeOffset);dkShaderInitialize(&shader,&maker);check(dkShaderIsValid(&shader)&&dkShaderGetStage(&shader)==expected,"shader stage/code mismatch");codeOffset+=alignUp(length,DK_SHADER_CODE_ALIGNMENT);
 }
public:
 explicit Renderer(const nx::game::RenderPack&value,const nx::game::MaterialPack&materialPack,const nx::game::GraphPack&definitions,const nx::game::WorldGraph&graph):scene(value){
  std::string error;checkSession(plan.initialize(scene,definitions,graph,error),error);
  DkDeviceMaker dm;dkDeviceMakerDefaults(&dm);dm.cbDebug=gpuMessage;device=dkDeviceCreate(&dm);check(device,"deko device");
  DkQueueMaker qm;dkQueueMakerDefaults(&qm,device);qm.flags=DkQueueFlags_Graphics;queue=dkQueueCreate(&qm);check(queue,"graphics queue");
  DkImageLayoutMaker im;dkImageLayoutMakerDefaults(&im,device);im.dimensions[0]=Width;im.dimensions[1]=Height;
  im.flags=DkImageFlags_UsageRender|DkImageFlags_UsagePresent|DkImageFlags_HwCompression|DkImageFlags_Usage2DEngine;im.format=DkImageFormat_RGBA8_Unorm;
  DkImageLayout colorLayout;dkImageLayoutInitialize(&colorLayout,&im);uint32_t stride=alignUp(dkImageLayoutGetSize(&colorLayout),dkImageLayoutGetAlignment(&colorLayout));
  colorMemory=allocate(stride*Frames,DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
  const DkImage*images[Frames];for(unsigned i=0;i<Frames;i++){dkImageInitialize(&colors[i],&colorLayout,colorMemory,i*stride);images[i]=&colors[i];}
  im.flags=DkImageFlags_UsageRender|DkImageFlags_HwCompression;im.format=DkImageFormat_Z24S8;
  DkImageLayout depthLayout;dkImageLayoutInitialize(&depthLayout,&im);depthMemory=allocate(dkImageLayoutGetSize(&depthLayout),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);dkImageInitialize(&depth,&depthLayout,depthMemory,0);
  DkSwapchainMaker sm;dkSwapchainMakerDefaults(&sm,device,nwindowGetDefault(),images,Frames);swapchain=dkSwapchainCreate(&sm);check(swapchain,"swapchain");
  constexpr uint32_t dataFlags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached;
  codeMemory=allocate(128*1024,dataFlags|DkMemBlockFlags_Code);
  loadShader(vertex,"romfs:/shaders/owned_world_vsh.dksh",DkStage_Vertex);loadShader(fragment,"romfs:/shaders/material_fsh.dksh",DkStage_Fragment);
  materials=std::make_unique<nx::game::GpuMaterials>();checkSession(materials->initialize(device,queue,materialPack,scene,error),error);
  report("[native-materials] %zu original materials, %zu decoded textures, %zu pixel bytes uploaded; native base/emissive pass, complete lighting/transparency/shader parity not implemented\n",materialPack.materials.size(),materialPack.textures.size(),materialPack.pixels.size());
  indexOffset=alignUp(scene.vertices.size()*sizeof(nx::Vertex),DK_MEMBLOCK_ALIGNMENT);
  geometryMemory=allocate(indexOffset+scene.indices.size()*sizeof(uint32_t),dataFlags);
  std::memcpy(dkMemBlockGetCpuAddr(geometryMemory),scene.vertices.data(),scene.vertices.size()*sizeof(nx::Vertex));
  std::memcpy((uint8_t*)dkMemBlockGetCpuAddr(geometryMemory)+indexOffset,scene.indices.data(),scene.indices.size()*4);
  uniformMemory=allocate(Frames*scene.owners.size()*DK_UNIFORM_BUF_ALIGNMENT,dataFlags);
  for(auto&frame:frames){DkCmdBufMaker cm;dkCmdBufMakerDefaults(&cm,device);frame.commands=dkCmdBufCreate(&cm);check(frame.commands,"command buffer");frame.memory=allocate(CommandBytes,dataFlags);}
  report("[native] deko3d geometry uploaded: %zu vertices, %zu triangles, %zu owned chunks, %zu renderers; hierarchy visibility/model transforms connected, original materials/textures incomplete\n",scene.vertices.size(),scene.indices.size()/3,scene.chunks.size(),scene.owners.size());
 }
 ~Renderer(){
  dkQueueWaitIdle(queue);for(auto&f:frames){dkCmdBufDestroy(f.commands);dkMemBlockDestroy(f.memory);}
  materials.reset();dkSwapchainDestroy(swapchain);dkQueueDestroy(queue);
  if(readbackMemory)dkMemBlockDestroy(readbackMemory);
  for(auto memory:{uniformMemory,geometryMemory,codeMemory,depthMemory,colorMemory})dkMemBlockDestroy(memory);
  dkDeviceDestroy(device);
 }
 unsigned draw(const nx::Camera&camera,bool capture){
  auto&frame=frames[frameIndex++%Frames];
  if(frame.submitted)check(dkFenceWait(&frame.fence,2000000000LL)==DkResult_Success,"frame fence timeout");
  std::string planError;checkSession(plan.prepare(camera,float(Width)/Height,planError),planError);check(plan.drawList().size()<=8192,"native visible draw command budget exceeded");
  int image=dkQueueAcquireImage(queue,swapchain);check(image>=0&&image<int(Frames),"swapchain acquire");
  DkCmdBuf cb=frame.commands;dkCmdBufClear(cb);dkCmdBufAddMemory(cb,frame.memory,0,CommandBytes);
  DkImageView target,depthTarget;dkImageViewDefaults(&target,&colors[image]);dkImageViewDefaults(&depthTarget,&depth);dkCmdBufBindRenderTarget(cb,&target,&depthTarget);
  DkViewport viewport{0,0,float(Width),float(Height),0,1};DkScissor scissor{0,0,Width,Height};dkCmdBufSetViewports(cb,0,&viewport,1);dkCmdBufSetScissors(cb,0,&scissor,1);
  dkCmdBufClearColorFloat(cb,0,DkColorMask_RGBA,.015f,.01f,.015f,1);dkCmdBufClearDepthStencil(cb,true,1,255,0);
  DkRasterizerState raster;dkRasterizerStateDefaults(&raster);raster.cullMode=DkFace_None;
  DkDepthStencilState depthState;dkDepthStencilStateDefaults(&depthState);
  DkColorState colorState;dkColorStateDefaults(&colorState);DkColorWriteState writeState;dkColorWriteStateDefaults(&writeState);
  dkCmdBufBindRasterizerState(cb,&raster);dkCmdBufBindDepthStencilState(cb,&depthState);dkCmdBufBindColorState(cb,&colorState);dkCmdBufBindColorWriteState(cb,&writeState);
  const DkShader*shaders[]={&vertex,&fragment};dkCmdBufBindShaders(cb,DkStageFlag_GraphicsMask,shaders,2);
  materials->bindSet(cb);
  DkGpuAddr uniforms=dkMemBlockGetGpuAddr(uniformMemory)+((frameIndex-1)%Frames)*scene.owners.size()*DK_UNIFORM_BUF_ALIGNMENT;
  const DkVtxAttribState attributes[]={
   {0,0,offsetof(nx::Vertex,position),DkVtxAttribSize_3x32,DkVtxAttribType_Float,0},
   {0,0,offsetof(nx::Vertex,normal),DkVtxAttribSize_3x32,DkVtxAttribType_Float,0},
   {0,0,offsetof(nx::Vertex,color),DkVtxAttribSize_4x8,DkVtxAttribType_Unorm,0},
   {0,0,offsetof(nx::Vertex,uv),DkVtxAttribSize_2x32,DkVtxAttribType_Float,0}};
  const DkVtxBufferState buffers[]={{sizeof(nx::Vertex),0}};
  dkCmdBufBindVtxAttribState(cb,attributes,4);dkCmdBufBindVtxBufferState(cb,buffers,1);dkCmdBufBindVtxBuffer(cb,0,dkMemBlockGetGpuAddr(geometryMemory),scene.vertices.size()*sizeof(nx::Vertex));dkCmdBufBindIdxBuffer(cb,DkIdxFormat_Uint32,dkMemBlockGetGpuAddr(geometryMemory)+indexOffset);
  unsigned draws=0;uint32_t lastOwner=UINT32_MAX,lastMaterial=UINT32_MAX;
  for(const auto&draw:plan.drawList()){
   if(draw.owner!=lastOwner){DkGpuAddr uniform=uniforms+draw.owner*DK_UNIFORM_BUF_ALIGNMENT;const auto&data=plan.uniform(draw.owner);dkCmdBufPushConstants(cb,uniform,DK_UNIFORM_BUF_ALIGNMENT,0,sizeof(data),&data);dkCmdBufBindUniformBuffer(cb,DkStage_Vertex,0,uniform,DK_UNIFORM_BUF_ALIGNMENT);lastOwner=draw.owner;}
   const auto&chunk=scene.chunks[draw.chunk];auto material=materials->materialForChunk(draw.chunk);if(material!=lastMaterial){materials->bind(cb,material);lastMaterial=material;}dkCmdBufDrawIndexed(cb,DkPrimitive_Triangles,chunk.indexCount,1,chunk.firstIndex,0,0);draws++;
  }
  dkCmdBufBarrier(cb,DkBarrier_Fragments,0);dkCmdBufDiscardDepthStencil(cb);
  dkCmdBufSignalFence(cb,&frame.fence,true);dkQueueSubmitCommands(queue,dkCmdBufFinishList(cb));frame.submitted=true;
  if(capture){
   // Complete graphics separately: a transfer failure must not be mistaken for
   // a shader/draw failure. Flush CPU-side submissions before a bounded wait.
   dkQueueFlush(queue);
   auto result=dkFenceWait(&frame.fence,2000000000LL);
   report("[native-gpu] draw fence=%d queue_error=%d draws=%u\n",int(result),int(dkQueueIsInErrorState(queue)),draws);
   check(result==DkResult_Success,"draw fence timeout");check(!dkQueueIsInErrorState(queue),"draw queue error");
   dkCmdBufClear(cb);dkCmdBufAddMemory(cb,frame.memory,0,CommandBytes);
   if(!readbackMemory)readbackMemory=allocate(Width*Height*4,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
   dkCmdBufBarrier(cb,DkBarrier_Full,0);
   // Zero selects tightly packed byte strides. Width is a pixel count and
   // cannot be used as the RGBA8 row stride (which would be Width*4 bytes).
   DkImageRect rect{0,0,0,Width,Height,1};DkCopyBuf output{dkMemBlockGetGpuAddr(readbackMemory),0,0};
   dkCmdBufCopyImageToBuffer(cb,&target,&rect,&output,0);dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_L2Cache);
   dkCmdBufSignalFence(cb,&frame.fence,true);dkQueueSubmitCommands(queue,dkCmdBufFinishList(cb));dkQueueFlush(queue);
   result=dkFenceWait(&frame.fence,2000000000LL);
   report("[native-gpu] readback fence=%d queue_error=%d\n",int(result),int(dkQueueIsInErrorState(queue)));
   check(result==DkResult_Success,"readback fence timeout");check(!dkQueueIsInErrorState(queue),"readback queue error");
   auto pixels=(const uint8_t*)dkMemBlockGetCpuAddr(readbackMemory);unsigned nonclear=0;
   for(unsigned i=0;i<Width*Height;i++)nonclear+=pixels[i*4]>16||pixels[i*4+1]>16||pixels[i*4+2]>16;
   report("[native-readback] nonclear_pixels=%u/%u; draw calls alone do not prove correct visual output\n",nonclear,Width*Height);
   FILE*imageFile=std::fopen("sdmc:/ultrakill-native.ppm","wb");
   if(imageFile){std::fprintf(imageFile,"P6\n%u %u\n255\n",Width,Height);for(unsigned y=0;y<Height;y++){uint8_t row[Width*3];for(unsigned x=0;x<Width;x++)std::memcpy(row+x*3,pixels+(y*Width+x)*4,3);std::fwrite(row,1,sizeof row,imageFile);}std::fclose(imageFile);}
  }
  dkQueuePresentImage(queue,swapchain,image);
  check(!dkQueueIsInErrorState(queue),"GPU queue error");return draws;
 }
};
float stick(int value){float v=value/32767.f;return std::abs(v)<.15f?0.f:(v-std::copysign(.15f,v))/.85f;}
struct FocusClock {
 AppletHookCookie cookie{};bool reset=false;
 static void event(AppletHookType type,void*data){if(type==AppletHookType_OnFocusState||type==AppletHookType_OnResume)static_cast<FocusClock*>(data)->reset=true;}
 FocusClock(){appletHook(&cookie,event,this);}
 ~FocusClock(){appletUnhook(&cookie);}
};
}

int main(){
 logfile=std::fopen("sdmc:/ultrakill-native.log","w");report("ULTRAKILL NX native migration: native joint/actor/material backend v11; full gameplay is incomplete\n");
 check(R_SUCCEEDED(romfsInit()),"RomFS init");
 nx::game::RenderPack scene;std::string renderError;checkSession(scene.load("romfs:/level1-2.nxrender",renderError),renderError);const char*error=nullptr;bool loaded=false;
 nx::game::MaterialPack materialPack;checkSession(materialPack.load("romfs:/level1-2.nxmat",renderError),renderError);
 nx::game::PhysicsPack physics;loaded=physics.load("romfs:/level1-2.nxphys",&error);check(loaded,error?error:"physics pack load");
 nx::game::GraphPack graph;std::string sessionError;checkSession(graph.load("romfs:/level1-2.nxgraph",sessionError),sessionError);
 nx::game::ActorPack actors;checkSession(actors.load("romfs:/level1-2.nxactor",sessionError),sessionError);
 nx::game::JointPack joints;checkSession(joints.load("romfs:/level1-2.nxjoint",sessionError),sessionError);
 nx::game::NativeSession session(physics,&graph,&actors,&joints);checkSession(session.initialize(2,sessionError),sessionError);
 unsigned activeNodes=0;for(uint32_t i=0;i<session.sceneGraph()->size();i++)activeNodes+=session.sceneGraph()->node(i)->active;
 report("[native-hierarchy] nodes=%zu components=%zu active=%u; player/probe pose binding connected, scene script/render activation backends incomplete\n",graph.nodes.size(),graph.components.size(),activeNodes);
 auto physicsCoverage=session.coverage();report("[native-physics] static=%u static_sensors=%u actor_bodies=%u actor_colliders=%u deferred_dynamic_colliders=%u; initial actor activation/kinematic targets, dynamic graph writeback and joint primitives connected; weapons/enemies/audio/UI not connected\n",physicsCoverage.staticBodies,physicsCoverage.sensors,physicsCoverage.actorBodies,physicsCoverage.actorColliders,physicsCoverage.dynamicDeferred);
 report("[native-joints] %zu source joint definitions loaded; attach lazily with active bodies; configuration/frame boundaries host-tested, complete ragdoll and console behavior unverified\n",joints.joints.size());
 auto camera=scene.camera();padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
 {
  Renderer renderer(scene,materialPack,graph,*session.sceneGraph());FocusClock focusClock;uint64_t previous=armGetSystemTick(),start=previous;unsigned frames=0;uint64_t drawSum=0;bool playerView=true;nx::FixedClock clock;
  while(appletMainLoop()){
   const uint64_t now=armGetSystemTick();uint64_t nanoseconds=armTicksToNs(now-previous);previous=now;
   if(focusClock.reset){focusClock.reset=false;session.clearPendingInput();nanoseconds=0;start=now;frames=0;drawSum=0;}
   if(appletGetFocusState()!=AppletFocusState_InFocus){session.clearPendingInput();clock.setPaused(true);start=now;frames=0;drawSum=0;svcSleepThread(16000000);continue;}
   float dt=std::min(.05f,float(nanoseconds)*1e-9f);
   padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
   if(padGetButtonsDown(&pad)&HidNpadButton_Minus){playerView=!playerView;session.clearPendingInput();nanoseconds=0;report("[native-mode] %s\n",playerView?"player movement":"inspection camera (simulation paused)");}
   if(padGetButtonsDown(&pad)&HidNpadButton_Y){camera=scene.camera();if(playerView){checkSession(session.initialize(2,sessionError),sessionError);clock=nx::FixedClock{};previous=armGetSystemTick();nanoseconds=0;}}
   auto left=padGetStickPos(&pad,0),right=padGetStickPos(&pad,1);camera.yaw+=stick(right.x)*2.2f*dt;camera.pitch=std::clamp(camera.pitch+stick(right.y)*2.2f*dt,-1.5f,1.5f);
   const uint64_t held=padGetButtons(&pad);float speed=(held&HidNpadButton_StickL)?60.f:15.f;
   clock.setPaused(!playerView);clock.advance(nanoseconds);
   if(playerView){
    session.setJumpHeld(bool(held&HidNpadButton_B));
    session.setSlideHeld(bool(held&HidNpadButton_R));
    if(padGetButtonsDown(&pad)&HidNpadButton_B)check(session.requestJump(double(armTicksToNs(now))*1e-9),"invalid jump input timestamp");
    if(padGetButtonsDown(&pad)&HidNpadButton_L)session.requestDash();
    if(padGetButtonsDown(&pad)&HidNpadButton_R)check(session.requestSlide(double(armTicksToNs(now))*1e-9),"invalid slide input timestamp");
    clock.run(8,[&](float step){checkSession(session.tick(stick(left.x),stick(left.y),camera.yaw,step,sessionError),sessionError);});
    for(const auto&event:session.effectRequests())report("[native-effect-pending] jump event=%u a=%.3f b=%.3f; audio/animation backend not yet connected\n",unsigned(event.kind),event.a,event.b);
    for(const auto&event:session.dashEffectRequests())report("[native-effect-pending] dash event=%u; audio/animation/rumble backend not yet connected\n",unsigned(event.kind));
    session.clearEffectRequests();
    camera.position=session.cameraPosition(float(clock.interpolation()));
   }else{
    nx::Vec3 direction=camera.right()*stick(left.x)+camera.forward()*stick(left.y);
    if(held&HidNpadButton_R)direction.y+=1;
    if(held&HidNpadButton_L)direction.y-=1;
    if(nx::dot(direction,direction)>1)direction=nx::normalized(direction);
    camera.position=camera.position+direction*(speed*dt);
   }
   bool capture=bool(padGetButtonsDown(&pad)&HidNpadButton_X);drawSum+=renderer.draw(camera,capture);frames++;
   // A diagnostic SD screenshot intentionally pauses simulated time. Its disk
   // write duration must not become a burst of catch-up gameplay ticks.
   if(capture){previous=armGetSystemTick();start=previous;frames=0;drawSum=0;}
   const uint64_t performanceNow=armGetSystemTick();double elapsed=armTicksToNs(performanceNow-start)*1e-9;
   if(elapsed>=5){report("[native-perf] %.1f FPS mean_draws=%.1f pos=(%.2f %.2f %.2f) ticks=%llu backlog_ms=%.2f grounded=%d; incomplete gameplay, not full-game performance\n",frames/elapsed,frames?double(drawSum)/frames:0,camera.position.x,camera.position.y,camera.position.z,(unsigned long long)clock.ticks,clock.backlogNanoseconds()*1e-6,int(session.grounded()));frames=0;drawSum=0;start=performanceNow;}
  }
 }
 report("[native] clean shutdown\n");romfsExit();if(logfile)std::fclose(logfile);return 0;
}
