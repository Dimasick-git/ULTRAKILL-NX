#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Bring-up API: executes AMD luma on captured frames; never claims to generate
// or present an interpolated frame. Opaque sessions are destroyed on render thread.
void* nxb_fg_probe_create(uint32_t color,uint32_t width,uint32_t height);
void* nxb_fg_probe_callback(void);
int nxb_fg_probe_status(void* session);
#ifdef __cplusplus
}
#endif
