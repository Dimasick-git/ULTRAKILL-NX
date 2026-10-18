/* Capture failing shader pairs, without changing materials or successful links. */
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nx_shader_diag.h"
#include "nx_gameroot.h"
#include "util.h"

void nx_link_program(GLuint program) {
  glLinkProgram(program);
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
  return eglGetProcAddress(name);
}
