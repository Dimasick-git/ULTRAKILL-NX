# r51 / TEST15 — local physics query batching and contact reads

## Console evidence

The TEST14 feedback is preserved in `uk-build/r50-fps-feedback/debug.log`;
`latest.log` begins at the r50 boot banner. Its registered exception count stays
at zero. This proves neither absence of all bugs nor that the earlier exception
source was fixed for every level.

Combat has 17.5 FPS / 57.08 ms average / 140.77 ms p95, followed by 23–43 FPS
windows. Other combat sections reach roughly 45–60 FPS. UnityMain is around
81–85% of one core in the worst sequence. Present waiting is approximately
0.001 ms, whereas autoaim Update/LateUpdate together take about 3.4 ms. This is
evidence of a main-thread bottleneck, not proof that autoaim accounts for the
whole missing frame budget. The available release recorder markers number only
three: Main Thread and two waits; physics/scripts are absent.

SaltyNX maps its direct EGL FPS/FSR region. The game uses native composition
with FSR OFF at 960x540 world resolution. Several scene-settle resets execute,
with no logged resolution-adjustment request in this boot. Actual visibility
still needs a hardware/user check; camera callbacks do not prove correct pixels.
SaltySD continues to report 80 Hz; this is not asserted to be panel refresh.

## Changes

* Preserve global automatic transform synchronization. Only the audited
  CameraFrustumTargeter Update/LateUpdate read passes temporarily disable it,
  after an explicit Physics.SyncTransforms. The original state is restored by
  a C++ RAII scope on normal return, early return and managed/C++ unwind. Existing
  method timings include both synchronization and restoration. Portal cast-state
  traversal remains unchanged; other game scripts retain automatic syncing.
  No scan skips, target caching across frames, physics timestep changes or new
  spawn delays are introduced.
* Replace Collision.contacts array reads in ClimbStep.OnCollisionStay,
  NewMovement.OnCollisionEnter, MaliciousFace.HandleCollision,
  SpiderBody.OnCollisionEnter and Zapper.OnCollisionEnter with GetContact /
  contactCount. Keep contact order, normal/point use and collision response.
  Fifteen get_contacts call sites are removed, not fifteen allocations per
  collision: Unity may cache the legacy array within that Collision instance.
  Contact arrays are no longer requested by these readers. Global callback
  reuse remains unchanged because Collision*Message exposes serialized events
  whose listeners can outlive the callback.
* Add native PlayerLoop stage brackets for actual physics, fixed scripts,
  Update/LateUpdate scripts, AI, animation, renderer/skinning updates and frame
  finish. Insert delegates around existing native systems; retain their native
  pointers/order. No native system is replaced or skipped. Initialization and
  removal allocate the tree; sampling uses counters without per-frame lists or
  scene searches. Five-second aggregate elapsed times include waits/callbacks
  and may overlap. These measurements identify the remaining bottleneck; they
  are not presented as an optimization by themselves.

Unity's synchronization guidance:
https://unity.com/how-to/enhanced-physics-performance-smooth-gameplay
PlayerLoop API:
https://docs.unity.cn/Documentation/ScriptReference/LowLevel.PlayerLoop.html

## Validation and limits

Production managed-loop tests verify native pointer/delegate preservation,
1,000 unchanged executions, idempotent installation, removal preserving another
owner's nodes, double disable and unsupported-loop handling. Production physics
helper tests cover original true/false policies and nested scopes. The actual
generated RAII wrapper is compiled and tested for nesting, early return and
exception unwinding. Autoaim patch tests remove only the added scopes/include
and compare the full original bodies. Contact patch tests preserve control flow
and response operations; contact-model tests cover 0–127 contacts and bounds.

An independent Unity Editor API probe creates 32 BoxColliders without entering
Play Mode. Across 100 poses, 500 batched ray/overlap/bounds snapshots match the
automatic-sync baseline. Transform changes after the batch remain visible and
the original synchronization setting is retained. This validates that query
policy in the fixture, not all gameplay or Switch FPS.

Final export/native/IL2CPP compilation, stage/import checks and NSP verification
are recorded with the exact source/generated/debug snapshot in the build state.
The first import reported a transient new-script reference error; only the
completed successful export is staged.

Stable 60 FPS, full deko rendering, sustained frame generation and all-bugs-fixed
are **not achieved or claimed**. Unity GLES remains the main renderer; AMD/deko
frame generation is experimental and its prior budget fallback is preserved.
No SD settings changes, game copy, commit, push or publication in this turn.
