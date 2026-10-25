/* Capture failing shader pairs, without changing materials or successful links. */
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>
#include "nx_shader_diag.h"
#include "nx_gameroot.h"
#include "util.h"

static uint64_t compile_ticks,link_ticks,compile_max,link_max;
static unsigned compile_count,link_count,slow_samples;
static void record_shader(int link,GLuint object,uint64_t ticks) {
  uint64_t *total=link?&link_ticks:&compile_ticks,*maximum=link?&link_max:&compile_max;
  __atomic_fetch_add(total,ticks,__ATOMIC_RELAXED);
  __atomic_fetch_add(link?&link_count:&compile_count,1,__ATOMIC_RELAXED);
  uint64_t old=__atomic_load_n(maximum,__ATOMIC_RELAXED);
  while(old<ticks&&!__atomic_compare_exchange_n(maximum,&old,ticks,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
  if(ticks>=armNsToTicks(8000000ULL)&&__atomic_fetch_add(&slow_samples,1,__ATOMIC_RELAXED)<8)
    debugPrintf("[shader-cost] %s object=%u %.2fms CPU wall\n",link?"link":"compile",object,(double)armTicksToNs(ticks)/1e6);
}
void nx_compile_shader(GLuint shader) {
  uint64_t start=armGetSystemTick();glCompileShader(shader);record_shader(0,shader,armGetSystemTick()-start);
}
/* Бинарники программ (Mesa с кэшем шейдеров). glProgramBinary -- загрузка
 * из UnityShaderCache вместо компиляции и линковки, glGetProgramBinary -- Unity
 * сохраняет только что слинкованную программу. fail -- Mesa отвергла бинарник
 * (другая сборка Mesa или битый файл), Unity тогда собирает из исходника. */
extern void nvc0_code_cache_take_stats(uint32_t counts[3],uint64_t ns[2]) __attribute__((weak)); /* Mesa, nvc0_program.c */
typedef void (*program_binary_fn)(GLuint,GLenum,const void*,GLsizei);
typedef void (*get_program_binary_fn)(GLuint,GLsizei,GLsizei*,GLenum*,void*);
static program_binary_fn real_program_binary;
static get_program_binary_fn real_get_program_binary;
static uint64_t binary_load_ticks,binary_load_max,binary_save_ticks,binary_save_bytes;
static unsigned binary_load_count,binary_load_fail,binary_save_count;
static void nx_program_binary(GLuint program,GLenum format,const void *binary,GLsizei length) {
  uint64_t start=armGetSystemTick();
  real_program_binary(program,format,binary,length);
  uint64_t ticks=armGetSystemTick()-start;
  GLint linked=GL_FALSE;
  glGetProgramiv(program,GL_LINK_STATUS,&linked);
  if(!linked)__atomic_fetch_add(&binary_load_fail,1,__ATOMIC_RELAXED);
  __atomic_fetch_add(&binary_load_ticks,ticks,__ATOMIC_RELAXED);
  __atomic_fetch_add(&binary_load_count,1,__ATOMIC_RELAXED);
  uint64_t old=__atomic_load_n(&binary_load_max,__ATOMIC_RELAXED);
  while(old<ticks&&!__atomic_compare_exchange_n(&binary_load_max,&old,ticks,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
}
static void nx_get_program_binary(GLuint program,GLsizei size,GLsizei *length,GLenum *format,void *binary) {
  uint64_t start=armGetSystemTick();
  GLsizei got=0;
  real_get_program_binary(program,size,&got,format,binary);
  if(length)*length=got;
  __atomic_fetch_add(&binary_save_ticks,armGetSystemTick()-start,__ATOMIC_RELAXED);
  __atomic_fetch_add(&binary_save_bytes,(uint64_t)got,__ATOMIC_RELAXED);
  __atomic_fetch_add(&binary_save_count,1,__ATOMIC_RELAXED);
}

void nx_shader_timing_poll(void) {
  unsigned c=__atomic_exchange_n(&compile_count,0,__ATOMIC_RELAXED),l=__atomic_exchange_n(&link_count,0,__ATOMIC_RELAXED);
  uint64_t ct=__atomic_exchange_n(&compile_ticks,0,__ATOMIC_RELAXED),lt=__atomic_exchange_n(&link_ticks,0,__ATOMIC_RELAXED);
  uint64_t cm=__atomic_exchange_n(&compile_max,0,__ATOMIC_RELAXED),lm=__atomic_exchange_n(&link_max,0,__ATOMIC_RELAXED);
  __atomic_store_n(&slow_samples,0,__ATOMIC_RELAXED);
  if(c||l)debugPrintf("[shader-cost] window compile=%u total=%.2fms max=%.2fms link=%u total=%.2fms max=%.2fms CPU wall\n",c,(double)armTicksToNs(ct)/1e6,(double)armTicksToNs(cm)/1e6,l,(double)armTicksToNs(lt)/1e6,(double)armTicksToNs(lm)/1e6);
  unsigned bl=__atomic_exchange_n(&binary_load_count,0,__ATOMIC_RELAXED),bf=__atomic_exchange_n(&binary_load_fail,0,__ATOMIC_RELAXED);
  unsigned bs=__atomic_exchange_n(&binary_save_count,0,__ATOMIC_RELAXED);
  uint64_t blt=__atomic_exchange_n(&binary_load_ticks,0,__ATOMIC_RELAXED),blm=__atomic_exchange_n(&binary_load_max,0,__ATOMIC_RELAXED);
  uint64_t bst=__atomic_exchange_n(&binary_save_ticks,0,__ATOMIC_RELAXED),bsb=__atomic_exchange_n(&binary_save_bytes,0,__ATOMIC_RELAXED);
  if(bl||bs)debugPrintf("[shader-binary] window load=%u fail=%u total=%.2fms max=%.2fms save=%u total=%.2fms %lluKB\n",bl,bf,(double)armTicksToNs(blt)/1e6,(double)armTicksToNs(blm)/1e6,bs,(double)armTicksToNs(bst)/1e6,(unsigned long long)(bsb>>10));
  /* машинный код nvc0 из кэша Mesa (hit) против кодогена (miss, результат
   * сохраняется); uncached -- программы, которые кэш не берёт (compute, stream
   * output, gl_program_cache 0) */
  if(nvc0_code_cache_take_stats){
    uint32_t n[3];uint64_t ns[2];
    nvc0_code_cache_take_stats(n,ns);
    if(n[0]||n[1]||n[2])debugPrintf("[shader-code] window hit=%u total=%.2fms miss=%u total=%.2fms uncached=%u\n",n[0],(double)ns[0]/1e6,n[1],(double)ns[1]/1e6,n[2]);
  }
}

void nx_link_program(GLuint program) {
  uint64_t start=armGetSystemTick();glLinkProgram(program);record_shader(1,program,armGetSystemTick()-start);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked) return;
  static unsigned failures;
  unsigned id = __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED);
  if (id >= 8) return;
  char message[1024]; GLsizei length = 0;
  glGetProgramInfoLog(program, sizeof(message), &length, message);
  debugPrintf("[shader-link] FAIL sample=%u program=%u %.*s\n", id, program, (int)length, message);
  char directory[512]; snprintf(directory, sizeof(directory), "%s/shader-errors", game_root());
  mkdir(directory, 0777);
  GLuint shaders[8]; GLsizei count = 0;
  glGetAttachedShaders(program, 8, &count, shaders);
  for (int i = 0; i < count; ++i) {
    GLint size = 0, type = 0;
    glGetShaderiv(shaders[i], GL_SHADER_SOURCE_LENGTH, &size);
    glGetShaderiv(shaders[i], GL_SHADER_TYPE, &type);
    if (size <= 0 || size > 512*1024) {
      debugPrintf("[shader-link] source=%u size=%d not captured (limit 512 KiB)\n", shaders[i], size);
      continue;
    }
    char *source = malloc(size);
    if (!source) continue;
    glGetShaderSource(shaders[i], size, &length, source);
    char path[640];
    snprintf(path, sizeof(path), "%s/fail-%u-%d.%s.glsl", directory, id, i,
             type == GL_VERTEX_SHADER ? "vert" : type == GL_FRAGMENT_SHADER ? "frag" : "other");
    FILE *file = fopen(path, "wb");
    if (file) { fwrite(source, 1, length, file); fclose(file); }
    free(source);
  }
}

__eglMustCastToProperFunctionPointerType nx_egl_get_proc(const char *name) {
  if (name && !strcmp(name, "eglGetProcAddress"))
    return (__eglMustCastToProperFunctionPointerType)&nx_egl_get_proc;
  if (name && !strcmp(name, "glLinkProgram"))
    return (__eglMustCastToProperFunctionPointerType)&nx_link_program;
  if (name && (!strcmp(name, "glProgramBinary") || !strcmp(name, "glProgramBinaryOES"))) {
    if (!real_program_binary)
      real_program_binary = (program_binary_fn)eglGetProcAddress("glProgramBinary");
    return real_program_binary ? (__eglMustCastToProperFunctionPointerType)&nx_program_binary : NULL;
  }
  if (name && (!strcmp(name, "glGetProgramBinary") || !strcmp(name, "glGetProgramBinaryOES"))) {
    if (!real_get_program_binary)
      real_get_program_binary = (get_program_binary_fn)eglGetProcAddress("glGetProgramBinary");
    return real_get_program_binary ? (__eglMustCastToProperFunctionPointerType)&nx_get_program_binary : NULL;
  }
  /* Unity resolves much of GLES through this path. Use the same wrappers as
   * ELF imports/dlsym, otherwise framebuffer/draw diagnostics can be bypassed. */
  extern uintptr_t dynlib_find_export(const char *name);
  uintptr_t wrapped = dynlib_find_export(name);
  if (wrapped) return (__eglMustCastToProperFunctionPointerType)wrapped;
  return eglGetProcAddress(name);
}
