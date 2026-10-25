# r50 / TEST14 — native composition when FSR is off; scene recovery

## Evidence

The complete TEST13 log and settings are preserved in
`uk-build/r49-render-feedback`, with the last boot extracted as `latest.log`.
The SaltyNX bridge maps successfully. Overlay requests change scale and filter
and the render chain acknowledges the new sizes. That control path is working
in this run; this does not prove all telemetry/refresh fields are correct.
SaltySD still reports 80 Hz despite its 60 Hz request on this handheld. The
bridge does not falsify that service result as a measured physical refresh.

Combat windows include 13.4 FPS / 74.35 ms average / 98.82 ms p95, and other
windows around 15–20 FPS. UnityMain is commonly 80–90% of one core. Decreasing
resolution did not establish stable 60. More than 1,000 managed exceptions are
reported by the end of the run, mostly ArgumentOutOfRangeException. Their
precise caller is not present in the old null-only throw trace.

User reports that after a level transition the world disappears while weapon,
HUD and crosshair remain. Changing scale restores the world. The previous
health check only validated RT object creation and dimensions; those do not
prove that camera/native framebuffer bindings survived scene teardown.

## Changes

* FSR OFF with FG OFF now keeps the game's original composition camera. World
  resolution remains adjustable, but there is no extra NX display camera,
  no NX input/upscale render textures, and no NX FSR blit command buffer.
  Previously the added chain still existed with filtering disabled. This
  removes real work/resource transitions; its hardware FPS benefit is not yet
  measured. OFF is not labelled as a fictitious FSR upscaler.
* Enabling FSR creates its EASU/RCAS chain; disabling it releases that chain and
  restores the original composition target. Render-size acknowledgments also
  work in the native path.
* Full managed RT reset detaches world/HUD camera targets before SetupRTs,
  replaces the MRT array, binds the fresh attachments, clears shared depth and
  colour once, and restores the previous active render target even on failure.
* A scene transition gets one additional reset after a world/composition frame
  actually completed. Startup order can change after sceneLoaded. The reset
  state is cleared before recovering; no periodic rebuild or artificial
  resolution oscillation is installed. This is recovery for the reported
  symptom, not confirmation of the native driver root cause.
* Add bounded Unity engine timing recorders, only for available nanosecond
  metrics: Main Thread, Physics.Simulate, BehaviourUpdate, Camera.Render,
  WaitForTargetFPS and Gfx.WaitForPresentOnGfxThread. Resources are disposed on
  disable; unsupported/failing capture disables itself without a retry loop.
  Samples may overlap/include waits and must not be added as separate CPU work.
  API reference (supports Release Players):
  https://docs.unity3d.com/2022.3/Documentation/ScriptReference/Unity.Profiling.ProfilerRecorder.html
* Trace the first four ArgumentOutOfRangeException origins before unwinding.
  Keep the original exception preparation and throw. Do not suppress the fault
  or pretend every list bounds error has been fixed. Stack words are labelled
  return-address candidates, not a proven managed call chain.

## Validation / release gate

Host tests exercise the production NxFsr lifecycle against instrumented resource
stubs: 100 OFF frames allocate no extra FSR targets/display/blits; scene change
causes one settle recovery; ON creates the chain and OFF releases it; target
restoration, telemetry and event unsubscribe remain intact. This tests managed
ownership/branching, not Switch GPU output.

Production transition state is tested for no recovery before world completion,
next-frame one-shot behavior, invalidation of old-scene acknowledgments and no
repeated reset over thousands of frames. Production managed reset ordering is
tested for detachment before setup, fresh MRT handles, clear, and active-target
restoration on injected failure. The actual pre-unwind native callback is tested
with 100 throws: four samples, unchanged exception object/preparation/throw.

Final Unity export, patched IL2CPP/native builds, imports, NSP integrity and exact
source/generated/debug snapshots are recorded in `uk-build/current-work-r50.json`.
The initial intermediate native-composition export failed because recovered
width/height fields are private; the final source reads public RenderTexture
dimensions. Only the final successful export may be staged.

World recovery and FPS improvements require console verification. Stable 60 FPS
is not established, all exceptions are not fixed, and the main renderer remains
GLES. Physics timestep 0.008, game speed and spawn cadence are unchanged. Keep
the user's current FSR/scale choices. No game copy to SD, commits or publication.
