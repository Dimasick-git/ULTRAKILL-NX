# r49 / TEST13 — restore SaltyNX/FSR control and repair stain transfer

## Hardware evidence from TEST12

Logs and original settings are preserved in `uk-build/r48-fps-feedback`.
Last boot: r48, normal level enemies. Worst non-loading five-second window:
35.9 FPS, 27.82 ms average frame, 43.19 ms p95; UnityMain 76.5% of one core.
Other combat windows: 39–45 FPS, with UnityMain approximately 85–90%.
Autoaim Update now takes about 1–1.5 ms in those windows. These are different
gameplay samples, not a controlled before/after benchmark.

Shader compile/link still occurs in gameplay. Link samples reach tens to
hundreds of milliseconds. The bounded enemy warmup is not a complete cure.
Its logged scene passes warmed 57 and 66 variants in 246.5 and 81.9 ms.

The bridge logged `existing active producer rc=0x359` and abandoned setup.
No `[fsr-control]` mapping exists in the last boot. The game stayed on plain
bilinear 960x540 to 1280x720; external control was disconnected.

## Corrections

* Identify and reuse the first existing FPS block instead of placing a new
  block behind one overlays discover first. Recognise this port's full region
  by its adjacent FSR1 v3 / FSRC contract and backend signature. Previous
  versions could leave the active flag set on exit and allocate another block
  on every launch. Reusing our region prevents that allocation leak.
* Preserve an unknown active producer's FPS fields read-only. It no longer
  prevents independently reserving/reusing the compatible FSR control block.
  Our present hook does not double count or apply a second limiter when that
  producer owns telemetry. Its liveness on hardware remains to be checked.
* Mark our own telemetry inactive on normal process exit; do not unmap shared
  memory while render threads are still alive. Foreign blocks stay untouched.
* Load the persisted title-specific external scale/filter/sharpness even when
  the SaltySD service is absent. Previously that file was read only as a side
  effect of successfully mapping the FPS bridge.
* Repair the confirmed null previous-parent path in BloodstainParent.AddStain.
  If the old cosmetic parent is gone, continue the original new-parent/list
  assignment. When the old parent exists, its original detach call is unchanged.
  This fixes one identified source; it does not establish the cause of every
  other exception or the remaining list-index errors.
* FG defaults off for fresh profiles. Existing explicit settings still win.
  The available AMD/deko path remains experimental and too expensive to use
  as a performance improvement. No duplicated frames are reported as success.

For this console, the two original settings files were copied to the feedback
folder before edits. Only settings on SD changed: external FSR enabled, 75%
scale, sharpness 0.5; local FSR enabled, render height 540, FG off. No game/NSP
was copied to SD. This is a performance-oriented baseline, not a 60 FPS claim.

## Validation

Production C host tests cover repeated launch/reuse, independent controls with
an existing producer, unchanged foreign frame counter, shutdown ownership,
session-limit repair/retry/failure cleanup, exact FPS/frametime ABI and pacing.
Render-control tests exercise coherent publications, bounds, actual-size
acknowledgments and standalone saved settings without SaltySD.

The exact changed stain-transfer statement is compiled and exercised with both
present and null old parents. It continues after either case; all other method
code is verified identical and the patch is idempotent.

Unity source/metadata is unchanged from the successful r48 export. The r49
incremental IL2CPP compile passed 9 nodes with zero failures. Native compile
also passed. Full NSP integrity and source/symbol snapshots are recorded in
`uk-build/current-work-r49.json` after packaging.

## What a full deko3d renderer actually requires

The current Unity Android player exposes GLES3 calls to Mesa. Only the optional
AMD frame-generation compute graph uses deko3d. A build flag cannot replace the
closed engine's graphics backend. This port does not contain Unity's native
renderer source or an implemented Unity deko backend.

deko3d offers an explicit low-overhead graphics API:
https://github.com/devkitPro/deko3d
Using that advantage for the whole world requires a real renderer/backend:
mesh and skinned-mesh submission, materials/variants compiled for DKSH,
textures, lighting, portals, depth/MRT, particles, UI, resource lifetime and
presentation. Translating existing GLES calls at runtime would still be a
translation layer and must not be described as an offline/native conversion.

Remaining performance work must identify main-thread physics/AI/render costs,
eliminate remaining first-use shader stalls, and benchmark representative wave
spawns and Limbo transitions. A 60 FPS frame has a 16.67 ms budget. TEST13 has
not demonstrated that budget on the console and must not be advertised as a
stable-60 release. Physics timestep, game speed and spawn timing are unchanged.
No commits or GitHub publication.
