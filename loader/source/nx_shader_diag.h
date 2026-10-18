#pragma once
#include <GLES2/gl2.h>
#include <EGL/egl.h>
void nx_link_program(GLuint program);
__eglMustCastToProperFunctionPointerType nx_egl_get_proc(const char *name);
