# r48 / TEST12 — enemy activation stalls and CPU work

User clarification: normal level enemies, with a transient drop from about
60 to 30 FPS when a wave appears. Sandbox.SpawnableInstance messages in
the log do not establish that the user spawned extra enemies.

TEST11 feedback is preserved in uk-build/r47-fps-feedback. A heavy window
has CameraFrustumTargeter.Update at 3.515 ms mean / 5.322 ms max, with
UnityMain at 81.7% of one CPU. These are window observations, not proof
of the exact cause of the activation stall.

## Changes

* Autoaim: move the existing viewport / score rejection blocks before
  portal and occlusion raycasts. The original generated blocks are moved
  verbatim: score formula, strict comparison, candidate order, target
  assignment and occlusion checks on eligible candidates remain intact.
  No change to aim strength or update frequency. The patch fails closed
  if the generated implementation changes.
* Remove the native override forcing every game log through its filter.
  Keep the game's own logger policy and native error/performance logging.
  This avoids unnecessary informational message processing; it does not
  remove gameplay calls, suppress native exceptions or fix their causes.
* Prewarm at most 96 distinct shader/pass/keyword variants referenced by
  scene enemies, including inactive waves, once at scene load. Use shared
  materials. No enemy activation, simulation, per-frame searches or
  timing changes. Toggle Enemy shader preload in SWITCH settings; takes
  effect on subsequent scene loads. Additional load time is expected.
  This targets first-use shader stalls; benefit on Switch is unverified.
* Time ActivateArena.SpawnEnemy, EnemyIdentifier Awake/OnEnable/Start and
  SpawnableInstance Awake/BaseSave. Record bounded slow samples and
  five-second inclusive aggregates. Nested times must not be summed.
* Time actual GLES compile/link calls. Correlate shader-cost and spawn-cost
  with the FPS windows rather than inferring causes from adjacent log text.
* Display actual FG runtime state in SWITCH settings, including fallback.
  Trace individual AMD jobs on the first history frame to locate GPU cost.
  Job timings explicitly include diagnostic serialization overhead.

## Frame generation status

TEST11 presented exactly one generated extra frame, then automatically
disabled FG: the history frame cost about 127 ms, including approximately
40 ms GLES capture and 66 ms interpolation. This is not sustained frame
generation and does not meet the performance target. TEST12 retains the
budget guard. FSR upscaling and frame generation are separate features.

## Validation and limits

* Original filter blocks retained byte-for-byte apart from their position;
  patch idempotent. 10,000 candidate-list replays preserve target selection,
  including ties, NaNs, blocked candidates and portals. Query counts never
  increase in this model; this is not a hardware performance benchmark.
* Actual Unity Editor shader-variant construction, deduplication and WarmUp
  pass. Inactive renderer fixture and scene state stay unchanged. The
  recovered enemy DLL cannot be instantiated with AddComponent in this
  Editor fixture; no claim of a full enemy lifecycle test is made.
* Shader warmup reference:
  https://docs.unity3d.com/2022.3/Documentation/ScriptReference/ShaderVariantCollection.WarmUp.html

Physics timestep remains 0.008. No wave delays, game speed, AI cadence,
damage or hitstop changes. Stable 40–45 combat FPS is not yet verified.
No commits, push or game copy to SD. Install the PC NSP through DBI/MTP.
