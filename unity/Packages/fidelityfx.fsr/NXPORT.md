# ULTRAKILL-NX FSR 3.1 backend

Upstream: https://github.com/ndepoel/FSR3Unity

Revision: `71879e71aa72465a79bdf009d7d71f9f7ab83066` (2024-12-15).
This is the FSR 3.1 temporal upscaler, using AMD's FidelityFX shader sources
and Nico de Poel's Unity compute backend. It does not implement frame generation.
The upstream MIT notices remain in the source files and LICENSE.md.

Local changes for Unity 2022.3 GLES:

- Storage images use supported R32 and RGBA formats. Two-channel storage
  images have two unused channels; sampled RG values are unchanged.
- FrameInfo uses four R32 texels instead of one read/write RGBA texel.
  Exposure reads the first texel. A reset texture preserves the original
  `(0, 1, 0, 0)` initialization.
- The SPD mip-5 cross-workgroup read uses a globally coherent structured
  buffer; the texture copy remains available to later passes. Buffer values
  are rounded to FP16 to match the original mip texture precision.
- The luma pass binds only the mip level it actually writes, with compact
  UAV registers below the GLES image-unit limit. The SPD buffer has an explicit
  register; its allocation follows the actual rounded half-size mip dimensions.
- New-lock storage uses R32 instead of an unsupported scalar UNORM image.
- Destroy releases both GPU allocations and Unity RenderTexture objects.

The reconstruction equations, pass sequence, jitter and history logic remain
from upstream. Wider scalar formats use more memory and require measurement
on the target console. Compiler warnings about minimum GLES storage-buffer
limits remain; a successful PC GLES test does not establish Switch support.

Port integration: Assets/NxPort/Fsr/NxFsr3.cs. It captures world depth and
motion before HUD rendering, supplies jittered world projections, converts
Gamma color to linear, dispatches FSR, then converts output back to Gamma.
A composition-difference reactive mask protects HUD/post-effect pixels;
heavy post effects may reduce temporal accumulation. Scene/camera/size
changes destroy the context and reset history. Camera cuts also reset it.

The SWITCH panel exposes `FSR 3.1 TEST`, disabled by default. Missing formats
or unsupported kernels fall back to FSR 1 with an explicit log message.
GPU/driver errors and dispatch exceptions disable retries until the user toggles the mode,
preventing repeated history allocations. No stable-60-FPS claim is made.

The port copy shader declares both texture properties explicitly (required
for Graphics.Blit source binding) and uses a high-precision GLES depth sampler.
Editor GPU checks cover actual source/depth sampling, Gamma conversion and
reactive-mask texture binding, rejecting driver error logs.
