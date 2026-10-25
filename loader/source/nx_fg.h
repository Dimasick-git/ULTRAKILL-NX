#pragma once
#include <stdint.h>
#include <EGL/egl.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 uint32_t version,color,depth,motion,generated;
 uint32_t renderWidth,renderHeight,outputWidth,outputHeight;
} NxFgDescription;
typedef struct {
 uint64_t frameId;
 float milliseconds,nearPlane,farPlane,fovRadians,jitterX,jitterY;
 uint32_t reset,reserved;
} NxFgFrame;
void* nxb_fg_create(const NxFgDescription* description);
void* nxb_fg_enqueue(void* session,const NxFgFrame* frame);
void* nxb_fg_callback(void);
int nxb_fg_status(void* session);
int nxb_fg_available(void);
int nx_fg_runtime_state(void); // -2 over budget, -1 GPU fault, 0 idle, 1 capture, 2 presenting
void nx_fg_settings_changed(void);
// Called by the EGL swap wrapper. handled=0 means use normal Unity swap.
EGLBoolean nx_fg_present(EGLDisplay display,EGLSurface surface,int* handled);
#ifdef __cplusplus
}
#endif
