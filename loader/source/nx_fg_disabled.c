#include "nx_fg.h"
#include "nx_fg_runtime.h"
// Preserve the managed/native ABI without creating GPU devices, transfers,
// render callbacks or additional presents in the normal Unity/GLES build.
void* nxb_fg_create(const NxFgDescription* description) { (void)description; return 0; }
void* nxb_fg_enqueue(void* session,const NxFgFrame* frame) { (void)session;(void)frame;return 0; }
void* nxb_fg_callback(void) { return 0; }
int nxb_fg_status(void* session) { (void)session;return -1; }
int nxb_fg_available(void) { return 0; }
int nx_fg_runtime_state(void) { return 0; }
void nx_fg_settings_changed(void) { }
EGLBoolean nx_fg_present(EGLDisplay display,EGLSurface surface,int* handled) {
 (void)display;(void)surface;if(handled)*handled=0;return EGL_FALSE;
}
void* nxb_fg_probe_create(uint32_t color,uint32_t width,uint32_t height) { (void)color;(void)width;(void)height;return 0; }
void* nxb_fg_probe_callback(void) { return 0; }
int nxb_fg_probe_status(void* session) { (void)session;return -1; }
