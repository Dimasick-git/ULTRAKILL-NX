#pragma once
#include <stdint.h>
#include "so_util.h"
typedef struct {
  uint32_t width, height, fps_num, fps_den, frames, sample_rate, channels, reserved;
  uint64_t audio_frames;
} NxbVideoInfo;
int nxb_video_open(const char *name, uintptr_t player, int playing, NxbVideoInfo *info);
void nxb_video_close(int handle);
void *nxb_video_frame(int handle, int wanted, int *actual);
int nxb_video_audio(int handle, float *samples, int count);
void nxb_video_audio_seek(int handle, int frame);
double nxb_video_time(int handle);
int nxb_video_status(int handle);
void nxb_video_control(int handle, int action, double value);
void nxb_video_texture(int handle, uint32_t gc_handle);
void nx_video_install_hooks(so_module *mod);
void *nx_media_extractor_unavailable(void);
