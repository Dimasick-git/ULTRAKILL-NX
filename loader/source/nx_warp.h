/* nx_warp.h -- перепроецирование кадра по повороту камеры (прототип). */
#ifndef NX_WARP_H
#define NX_WARP_H

#include <stdint.h>

enum { NX_WARP_CAM_NONE, NX_WARP_CAM_MAIN, NX_WARP_CAM_HUD };

/* главный поток */
void nx_warp_install(uintptr_t ub);
int nx_warp_role(const char *camera_name);
void nx_warp_camera(int role, uintptr_t camera, int exit);   /* Camera::CustomRender */
void nx_warp_overlays(void);                                 /* вход UI::RenderOverlays */

/* поток рендера */
void nx_warp_before_swap(void *display, void *surface);     /* eglSwapBuffers Unity */
void nx_warp_after_swap(void);
int nx_warp_presenting(void);                                /* nwindowQueueBuffer: вставленный кадр */
int nx_warp_on_render_thread(void);
int64_t nx_warp_idle(void);   /* futex потока рендера: вставить кадр, если пора; нс до следующего или -1 */

#endif
