#pragma once
#include <GLES2/gl2.h>
#include <EGL/egl.h>
void nx_link_program(GLuint program);
void nx_compile_shader(GLuint shader);
void nx_shader_timing_poll(void);
__eglMustCastToProperFunctionPointerType nx_egl_get_proc(const char *name);
