// Isolated deko3d bring-up. It does not load or modify the game.
#include <switch.h>
#include <deko3d.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <vector>
#include <EGL/egl.h>
#include "nx_fg_interop.h"
#include "probe_layout.h"
static FILE* report;
static void log(const char* fmt,...) {
 va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);
 if(report){va_start(ap,fmt);vfprintf(report,fmt,ap);va_end(ap);fflush(report);}
 consoleUpdate(nullptr);
}
[[noreturn]] static void fail(const char* text) {
 log("FAIL: %s\n",text);if(report){fclose(report);report=nullptr;}
 // Run the libnx exit callbacks, including applet shutdown. svcExitProcess
 // bypassed them and the v2 timeout was followed by a qlaunch applet error.
 std::exit(EXIT_FAILURE);
}
#define CHECK(x) do {if(!(x))fail(#x);} while(0)
static void debug(void*,const char* where,DkResult result,const char* message){
 log("deko: %s result=%d %s\n",where,(int)result,message);
 if(result!=DkResult_Success)fail("deko validation error");
}
struct Context {
 DkDevice device{};DkQueue queue{};std::vector<DkMemBlock> blocks;
 Context(){
  DkDeviceMaker dm;dkDeviceMakerDefaults(&dm);dm.cbDebug=debug;device=dkDeviceCreate(&dm);CHECK(device);
  DkQueueMaker qm;dkQueueMakerDefaults(&qm,device);qm.flags|=DkQueueFlags_Compute;queue=dkQueueCreate(&qm);CHECK(queue);
 }
 ~Context(){dkQueueWaitIdle(queue);dkQueueDestroy(queue);for(auto m:blocks)dkMemBlockDestroy(m);dkDeviceDestroy(device);}
 DkMemBlock alloc(uint32_t size,uint32_t extra=0){
  CHECK(size<0xffff0000u);DkMemBlockMaker m;dkMemBlockMakerDefaults(&m,device,(size+4095)&~4095u);
  m.flags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached|DkMemBlockFlags_ZeroFillInit|extra;
  auto b=dkMemBlockCreate(&m);CHECK(b);blocks.push_back(b);return b;
 }
 DkCmdBuf begin(){DkCmdBufMaker cm;dkCmdBufMakerDefaults(&cm,device);auto cb=dkCmdBufCreate(&cm);CHECK(cb);
  auto mem=alloc(65536);dkCmdBufAddMemory(cb,mem,0,65536);return cb;}
 void run(DkCmdBuf cb,const char* stage="compute"){
  log("BEGIN: %s\n",stage);
  DkFence fence;dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_L2Cache);
  dkCmdBufSignalFence(cb,&fence,true);dkQueueSubmitCommands(queue,dkCmdBufFinishList(cb));dkQueueFlush(queue);
  // A hung GPU must not make a diagnostic application wait forever.
  auto result=dkFenceWait(&fence,2000000000LL);
  bool error=dkQueueIsInErrorState(queue);
  log("FENCE: %s result=%d queue_error=%d\n",stage,(int)result,(int)error);
  CHECK(result==DkResult_Success);CHECK(!error);dkCmdBufDestroy(cb);
 }
 DkShader shader(const char* name){
  char path[256];snprintf(path,sizeof path,"romfs:/shaders/%s.dksh",name);FILE* f=fopen(path,"rb");CHECK(f);
  uint32_t h[6];CHECK(fread(h,sizeof h,1,f)==1);CHECK(h[0]==0x48534b44 && h[1]==24 && h[5]==1);
  CHECK(h[2]>=24 && h[2]<=65536 && h[3]>0 && h[3]<=4*1024*1024);
  auto control=malloc(h[2]);CHECK(control);rewind(f);CHECK(fread(control,h[2],1,f)==1);
  auto code=alloc(h[3]+DK_SHADER_CODE_UNUSABLE_SIZE,DkMemBlockFlags_Code);
  CHECK(fread(dkMemBlockGetCpuAddr(code),h[3],1,f)==1);CHECK(fgetc(f)==EOF);fclose(f);
  DkShaderMaker sm;dkShaderMakerDefaults(&sm,code,0);sm.control=control;DkShader shader;dkShaderInitialize(&shader,&sm);
  free(control);CHECK(dkShaderIsValid(&shader));CHECK(dkShaderGetStage(&shader)==DkStage_Compute);return shader;
 }
 void bindShader(DkCmdBuf cb,const DkShader& s){const DkShader* p=&s;dkCmdBufBindShaders(cb,DkStageFlag_Compute,&p,1);}
 void storage(DkCmdBuf cb,unsigned slot,DkMemBlock b){DkBufExtents e{dkMemBlockGetGpuAddr(b),dkMemBlockGetSize(b)};dkCmdBufBindStorageBuffers(cb,DkStage_Compute,slot,&e,1);}
 void uniform(DkCmdBuf cb,unsigned slot,DkMemBlock b){DkBufExtents e{dkMemBlockGetGpuAddr(b),dkMemBlockGetSize(b)};dkCmdBufBindUniformBuffers(cb,DkStage_Compute,slot,&e,1);}
};
static void subgroupTest(Context& c){
 auto shader=c.shader("subgroup_probe");auto out=c.alloc(64*32);memset(dkMemBlockGetCpuAddr(out),0xcd,64*32);
 auto cb=c.begin();c.bindShader(cb,shader);c.storage(cb,0,out);dkCmdBufDispatchCompute(cb,1,1,1);c.run(cb);
 auto v=(const uint32_t*)dkMemBlockGetCpuAddr(out);
 for(unsigned i=0;i<64;i++){
  unsigned base=(i/32)*32,sum=0,min=999,max=0,count=0;
  CHECK(v[i*8+0]==(base+1+base+32)*16);CHECK(v[i*8+1]==v[i*8+0]);CHECK(v[i*8+2]==base+1);CHECK(v[i*8+3]==base+32);
  if(i%32%3==0){for(int j=4;j<8;j++)CHECK(v[i*8+j]==0);continue;}
  for(unsigned lane=0;lane<32;lane++)if(lane%3!=0){unsigned x=base+lane+1;sum+=x;if(x<min)min=x;if(x>max)max=x;count++;}
  CHECK(v[i*8+4]==sum);CHECK(v[i*8+5]==min);CHECK(v[i*8+6]==max);CHECK(v[i*8+7]==count);
 }
 log("PASS: subgroup full/partial active masks, integer/float reductions (64 lanes)\n");
}
static void pyramidTest(Context& c){
 auto shader=c.shader("pyramid_probe");auto packed=c.alloc(13*16*8);auto layout=c.alloc(13*16);auto out=c.alloc(13*16);
 auto levels=(uint32_t*)dkMemBlockGetCpuAddr(layout);
 for(unsigned m=0;m<13;m++){levels[m*4]=m*16;levels[m*4+1]=levels[m*4+2]=4;levels[m*4+3]=0;}
 memset(dkMemBlockGetCpuAddr(packed),0xad,13*16*8);
 auto cb=c.begin();c.bindShader(cb,shader);c.storage(cb,PYRAMID_STORAGE_SLOT,packed);c.storage(cb,PYRAMID_RESULT_SLOT,out);c.uniform(cb,PYRAMID_LAYOUT_SLOT,layout);
 dkCmdBufDispatchCompute(cb,1,1,1);c.run(cb);
 auto raw=(const uint32_t*)dkMemBlockGetCpuAddr(packed);auto result=(const float*)dkMemBlockGetCpuAddr(out);
 for(unsigned m=0;m<13;m++){
  CHECK(raw[(m*16+5)*2]==pyramid_expected[m][0]);CHECK((raw[(m*16+5)*2+1] & (m==0?~0x8000u:~0u))==(pyramid_expected[m][1] & (m==0?~0x8000u:~0u)));
  for(unsigned pixel=0;pixel<16;pixel++)if(pixel!=5){CHECK(raw[(m*16+pixel)*2]==0xadadadad);CHECK(raw[(m*16+pixel)*2+1]==0xadadadad);}
  CHECK(result[m*4]==float(m));CHECK(result[m*4+1]==float(m)+0.5f);CHECK(result[m*4+2]==-float(m));CHECK(result[m*4+3]==1.0f);
 }
 log("PASS: thirteen RGBA16F mip ranges, half packing, bounds and read-after-write\n");
}
struct Image {DkImage object;DkImageView view;};
static Image image(Context& c,DkImageFormat fmt,unsigned w,unsigned h,uint32_t flags){
 DkImageLayoutMaker lm;dkImageLayoutMakerDefaults(&lm,c.device);lm.format=fmt;lm.dimensions[0]=w;lm.dimensions[1]=h;lm.dimensions[2]=1;lm.flags=flags;
 DkImageLayout layout;dkImageLayoutInitialize(&layout,&lm);CHECK(dkImageLayoutGetSize(&layout)<16*1024*1024);
 auto mem=c.alloc((uint32_t)dkImageLayoutGetSize(&layout),DkMemBlockFlags_Image);
 Image result;dkImageInitialize(&result.object,&layout,mem,0);return result;
}
static void lumaTest(Context& c){
 constexpr unsigned W=64,H=64;auto shader=c.shader("ffx_opticalflow_prepare_luma_pass");
 auto source=c.alloc(W*H*4);auto dest=c.alloc(W*H+256);auto constants=c.alloc(256);auto descriptors=c.alloc(256);auto samplers=c.alloc(256);
 auto data=(uint8_t*)dkMemBlockGetCpuAddr(source);for(unsigned i=0;i<W*H;i++){data[i*4]=(i*17)%256;data[i*4+1]=(i*37+3)%256;data[i*4+2]=(i*73+5)%256;data[i*4+3]=255;}
 memset(dkMemBlockGetCpuAddr(dest),0xcd,W*H+256);
 // AMD cbOF: inputSize, pyramidLevel, levelCount, frameIndex, transferFunction, min/max luminance.
 uint32_t params[8]={W,H,0,7,0,0,0,0};memcpy(dkMemBlockGetCpuAddr(constants),params,sizeof params);
 auto input=image(c,DkImageFormat_RGBA8_Unorm,W,H,0);auto output=image(c,DkImageFormat_R8_Uint,W,H,DkImageFlags_UsageLoadStore);
 dkImageViewDefaults(&input.view,&input.object);dkImageViewDefaults(&output.view,&output.object);
 auto desc=(DkImageDescriptor*)dkMemBlockGetCpuAddr(descriptors);dkImageDescriptorInitialize(&desc[0],&input.view,false,false);dkImageDescriptorInitialize(&desc[1],&output.view,true,false);
 DkSampler sampler;dkSamplerDefaults(&sampler);for(int i=0;i<3;i++)sampler.wrapMode[i]=DkWrapMode_ClampToEdge;
 dkSamplerDescriptorInitialize((DkSamplerDescriptor*)dkMemBlockGetCpuAddr(samplers),&sampler);
 // deko's rowLength/imageHeight are BYTE strides, unlike GL pixel-store
 // dimensions. Zero selects tightly packed strides from the image format.
 // W for RGBA8 was a 64-byte pitch for a 256-byte row, invalid for the DMA.
 DkImageRect rect{0,0,0,W,H,1};DkCopyBuf upload{dkMemBlockGetGpuAddr(source),0,0};DkCopyBuf download{dkMemBlockGetGpuAddr(dest),0,0};
 auto cb=c.begin();dkCmdBufCopyBufferToImage(cb,&upload,&input.view,&rect,0);
 c.run(cb,"RGBA8 buffer-to-image upload");
 auto roundtrip=c.alloc(W*H*4);DkCopyBuf roundtripCopy{dkMemBlockGetGpuAddr(roundtrip),0,0};
 cb=c.begin();dkCmdBufCopyImageToBuffer(cb,&input.view,&rect,&roundtripCopy,0);
 c.run(cb,"RGBA8 image-to-buffer roundtrip");
 CHECK(memcmp(data,dkMemBlockGetCpuAddr(roundtrip),W*H*4)==0);
 log("PASS: RGBA8 image upload/download, all 16384 bytes identical\n");
 // Separate ordinary texture/image access from AMD code generation. A timeout
 // identifies whether copies, sampled descriptors, stores, or AMD math failed.
 auto samples=c.alloc(W*H*16);auto textureShader=c.shader("texture_probe");
 cb=c.begin();c.bindShader(cb,textureShader);
 dkCmdBufBindImageDescriptorSet(cb,dkMemBlockGetGpuAddr(descriptors),2);
 dkCmdBufBindSamplerDescriptorSet(cb,dkMemBlockGetGpuAddr(samplers),1);
 DkResHandle tex=dkMakeTextureHandle(0,0),uav=dkMakeImageHandle(1);
 dkCmdBufBindTextures(cb,DkStage_Compute,0,&tex,1);c.storage(cb,0,samples);
 dkCmdBufDispatchCompute(cb,8,8,1);c.run(cb,"minimal texture fetch to SSBO");
 auto sampled=(const float*)dkMemBlockGetCpuAddr(samples);
 for(unsigned i=0;i<W*H*4;i++)CHECK(fabsf(sampled[i]-data[i]/255.0f)<0.00001f);
 log("PASS: sampled RGBA8 descriptor, 16384 components vs CPU\n");
 auto imageShader=c.shader("image_probe");cb=c.begin();c.bindShader(cb,imageShader);
 dkCmdBufBindImages(cb,DkStage_Compute,0,&uav,1);
 dkCmdBufDispatchCompute(cb,8,8,1);c.run(cb,"minimal R8 image store");
 cb=c.begin();dkCmdBufCopyImageToBuffer(cb,&output.view,&rect,&download,0);
 c.run(cb,"minimal R8 image readback");
 auto stored=(const uint8_t*)dkMemBlockGetCpuAddr(dest);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)CHECK(stored[y*W+x]==((x+3*y)&255));
 for(unsigned i=W*H;i<W*H+256;i++)CHECK(stored[i]==0xcd);
 log("PASS: R8 storage image descriptor and guarded readback\n");
 memset(dkMemBlockGetCpuAddr(dest),0xcd,W*H+256);
 cb=c.begin();dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
 c.bindShader(cb,shader);dkCmdBufBindImageDescriptorSet(cb,dkMemBlockGetGpuAddr(descriptors),2);dkCmdBufBindSamplerDescriptorSet(cb,dkMemBlockGetGpuAddr(samplers),1);
 dkCmdBufBindTextures(cb,DkStage_Compute,LUMA_INPUT_SLOT,&tex,1);dkCmdBufBindImages(cb,DkStage_Compute,LUMA_OUTPUT_SLOT,&uav,1);c.uniform(cb,LUMA_CONSTANT_SLOT,constants);
 dkCmdBufDispatchCompute(cb,2,2,1);c.run(cb,"AMD prepare_luma dispatch");
 cb=c.begin();dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_Image);
 dkCmdBufCopyImageToBuffer(cb,&output.view,&rect,&download,0);c.run(cb,"R8 luma image download");
 auto result=(const uint8_t*)dkMemBlockGetCpuAddr(dest);
 for(unsigned i=0;i<W*H;i++){int expected=(int)(0.2126f*data[i*4]+0.7152f*data[i*4+1]+0.0722f*data[i*4+2]);CHECK(abs((int)result[i]-expected)<=1);}
 for(unsigned i=W*H;i<W*H+256;i++)CHECK(result[i]==0xcd);
 log("PASS: actual AMD prepare_luma shader, 4096 pixels vs CPU reference\n");
}
static GLuint glTexture(GLint internal,GLenum format,unsigned w,unsigned h,const void* bytes=nullptr){
 GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,GL_UNSIGNED_BYTE,bytes);return texture;
}
static void interopTest(Context& c){
 log("BEGIN: GLES/deko shared-PBO interop (same implementation linked by loader)\n");
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);CHECK(display!=EGL_NO_DISPLAY);
 CHECK(eglInitialize(display,nullptr,nullptr));CHECK(eglBindAPI(EGL_OPENGL_ES_API));
 EGLConfig config;EGLint count=0;
 EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
  EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 EGLSurface surface=EGL_NO_SURFACE;
 if(eglChooseConfig(display,attributes,&config,1,&count)&&count){
  EGLint size[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};surface=eglCreatePbufferSurface(display,config,size);
 }
 if(surface==EGL_NO_SURFACE){
  log("GLES: no pbuffer; using surfaceless context for offscreen FBO test\n");
  // The libnx text console already owns the native window/buffer queue.
  // Never create a second window surface against that same NWindow.
  attributes[1]=0;CHECK(eglChooseConfig(display,attributes,&config,1,&count)&&count);
 }
 EGLint version[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);CHECK(context!=EGL_NO_CONTEXT);
 CHECK(eglMakeCurrent(display,surface,surface,context));
 log("GLES: %s / %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
 constexpr unsigned W=64,H=64;std::vector<uint8_t> pixels(W*H*4);
 for(unsigned i=0;i<W*H;i++){pixels[i*4]=i*17;pixels[i*4+1]=i*37+3;pixels[i*4+2]=i*73+5;pixels[i*4+3]=255;}
 auto inputTexture=glTexture(GL_RGBA8,GL_RGBA,W,H,pixels.data());
 auto outputTexture=glTexture(GL_R8,GL_RED,W,H);
 {
  nxfg::Transfer input,output;
  if(!input.Initialize(c.device,W,H,GL_RGBA,GL_UNSIGNED_BYTE,4))fail(input.Error());
  if(!output.Initialize(c.device,W,H,GL_RED,GL_UNSIGNED_BYTE,1))fail(output.Error());
  log("PASS: persistent GL PBO pages registered with deko (no CPU pixel copy)\n");
  CHECK(input.Capture(inputTexture));
  log("PASS: GLES capture fence completed\n");
  auto inImage=image(c,DkImageFormat_RGBA8_Unorm,W,H,0);
  auto outImage=image(c,DkImageFormat_R8_Uint,W,H,DkImageFlags_UsageLoadStore);
  dkImageViewDefaults(&inImage.view,&inImage.object);dkImageViewDefaults(&outImage.view,&outImage.object);
  auto constants=c.alloc(256),descriptors=c.alloc(256),samplers=c.alloc(256);
  uint32_t params[8]={W,H,0,7,0,0,0,0};memcpy(dkMemBlockGetCpuAddr(constants),params,sizeof params);
  auto desc=(DkImageDescriptor*)dkMemBlockGetCpuAddr(descriptors);
  dkImageDescriptorInitialize(desc,&inImage.view,false,false);dkImageDescriptorInitialize(desc+1,&outImage.view,true,false);
  DkSampler sampler;dkSamplerDefaults(&sampler);dkSamplerDescriptorInitialize((DkSamplerDescriptor*)dkMemBlockGetCpuAddr(samplers),&sampler);
  auto shader=c.shader("ffx_opticalflow_prepare_luma_pass");auto cb=c.begin();
  DkImageRect rect{0,0,0,W,H,1};DkCopyBuf fromGl{input.Address(),0,0},toGl{output.Address(),0,0};
  dkCmdBufCopyBufferToImage(cb,&fromGl,&inImage.view,&rect,0);
  dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
  c.bindShader(cb,shader);c.uniform(cb,LUMA_CONSTANT_SLOT,constants);
  dkCmdBufBindImageDescriptorSet(cb,dkMemBlockGetGpuAddr(descriptors),2);dkCmdBufBindSamplerDescriptorSet(cb,dkMemBlockGetGpuAddr(samplers),1);
  DkResHandle texture=dkMakeTextureHandle(0,0),imageHandle=dkMakeImageHandle(1);
  dkCmdBufBindTextures(cb,DkStage_Compute,LUMA_INPUT_SLOT,&texture,1);dkCmdBufBindImages(cb,DkStage_Compute,LUMA_OUTPUT_SLOT,&imageHandle,1);
  dkCmdBufDispatchCompute(cb,2,2,1);dkCmdBufBarrier(cb,DkBarrier_Full,DkInvalidateFlags_Image);
  dkCmdBufCopyImageToBuffer(cb,&outImage.view,&rect,&toGl,0);c.run(cb,"shared PBO -> AMD luma -> shared PBO");
  CHECK(output.Upload(outputTexture));CHECK(output.WaitConsumer());
  GLuint framebuffer;glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,outputTexture,0);
  CHECK(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glReadBuffer(GL_COLOR_ATTACHMENT0);
  std::vector<uint8_t> actual(W*H*4);
  // CPU readback exists ONLY in this diagnostic comparison, never in Transfer.
  glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
  CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned i=0;i<W*H;i++){
   int expected=int(0.2126f*pixels[i*4]+0.7152f*pixels[i*4+1]+0.0722f*pixels[i*4+2]);
   CHECK(abs(int(actual[i*4])-expected)<=1);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glDeleteFramebuffers(1,&framebuffer);
  log("PASS: GLES -> deko AMD prepare_luma -> GLES, 4096 pixels vs CPU\n");
 }
 glDeleteTextures(1,&inputTexture);glDeleteTextures(1,&outputTexture);
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 eglDestroyContext(display,context);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);eglTerminate(display);
}
#include "sdk_gpu_probe.h"
int main(int,char**){
 consoleInit(nullptr);report=fopen("sdmc:/fsr31-deko-probe.log","w");
 log("ULTRAKILL NX - isolated FSR 3.1 deko probe v9\nThis is NOT the game. Full GPU graph is experimental.\n");
 CHECK(R_SUCCEEDED(romfsInit()));{Context c;subgroupTest(c);pyramidTest(c);lumaTest(c);interopTest(c);}sdkGpuTest();
 log("ALL GPU CHECKS PASSED\nPress PLUS to exit.\n");if(report){fclose(report);report=nullptr;}
 PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
 while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}
 romfsExit();consoleExit(nullptr);return 0;
}
