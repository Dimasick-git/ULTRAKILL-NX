#pragma once
#include <stdint.h>
/* FSR-NX overlay's FSR1 v3 + FSRC24 wire contract, independent GLES backend. */
typedef struct {
  uint32_t magic;
  uint8_t version,enabled,mode,status;
  float sharpness;
  uint16_t in_width,in_height,out_width,out_height;
  uint32_t frames,error,gpu_us,log_seq;
  char log[88];
  uint16_t scale_percent,scaled_count;
} NxFsrShared;
typedef struct {
  uint32_t magic,seq,applied;
  uint16_t scale,max_scale;
  uint8_t fsr,depth,lazy,shader_fix,active,upscaler,native,hdr;
} NxFsrLive;
_Static_assert(sizeof(NxFsrShared)==128,"FSR1 v3 ABI");
_Static_assert(sizeof(NxFsrLive)==24,"FSRC ABI");
void nx_render_shared_init(void *region);
int nx_render_height(void);
int nx_render_filter(void);
float nx_render_sharpness(void);
void nxb_fsr_applied(int width,int height,int output_width,int output_height,int active);
