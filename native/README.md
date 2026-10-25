# ULTRAKILL NX — native migration

This directory is the independent C++/deko3d implementation in progress.
It does not replace the working Unity port in `loader/` and `unity/`.
No Unity player, GLES or Mesa is linked into the native executable.
**It is not a playable ULTRAKILL port yet.**

## Implemented

* Offline conversion of actual level 1-2 geometry, hierarchy transforms,
  static-batch submesh ranges, indices, normals, UVs and the original camera.
  External mesh resolution includes `models.bundle`. Undefined source UVs are
  replaced with zero and counted in the coverage report. No gameplay assets
  are checked into the source tree.
* A versioned binary scene format with allocation budgets, exact payload length,
  CRC, finite-value checks, index/range checks and geometry/bounds validation.
* Native deko3d swapchain, depth rendering, precompiled DKSH shaders, indexed
  geometry, spatial chunk frustum culling, two frame slices with bounded fences,
  explicit GPU resource ownership and shutdown.
* Native controller input and free camera. Dev9 connects original base/emissive
  textures, UV transforms, tint and vertex colours through a native texture
  pass. Complete original lighting, blending, shader effects and GPU visual
  validation remain pending.
* The owned renderer preserves all 3,811 MeshRenderer owners in level 1-2,
  including initially inactive objects, rigid bodies and animation rest poses,
  and their exact material references. Geometry is renderer-local;
  current hierarchy transforms, enabled state, layer and inherited activity
  drive draw selection. Cached world bounds and inverse-transpose normals
  update when an owner moves. Host checks cover transformed culling and the
  original static-batch/collider coordinate agreement. GPU execution of this
  owned rendering path has not yet been checked on the console.
* Dev10 retains 1,450 authored rigid bodies and 1,715 compound-collider
  references. Four initial kinematic objects instantiate eleven original
  trigger colliders. Native compound dynamics preserve per-child query and
  simulation layers, collider identity and materials; separate sensor bodies
  follow their owner. Hierarchy activation, kinematic targets and dynamic
  pose writeback pass host checks. Only active bodies are visited each tick.
  Runtime collider rebuilding/layer changes, explicit inertia,
  complete player helper bodies and scene-script lifecycle remain pending.
  Unsupported geometry mutations fail explicitly.
* Dev11 retains all 986 authored joints (956 CharacterJoint, 14 FixedJoint,
  16 ConfigurableJoint). Jolt constraints attach lazily to active bodies;
  local reference frames survive activation and motion-mode changes. Connected
  collision exclusions, break events and linear anchor projection are handled.
  Unity 2022.3 reference runs verify twist signs and zero-secondary-axis frame
  construction; 119 original joints use that fallback. A host coverage audit
  creates all 1,449 nonplayer bodies and individually attaches all 986 joints
  without errors. This is not a complete animated ragdoll or gameplay test.
  Soft limits, drives, mass scaling and full PhysX projection parity remain
  outside the implemented adapter. Unsupported configurations fail explicitly.
  Zero-scale box extents and fully frozen bodies also have reference checks
  against Unity 2022.3; tiny shapes remain queryable rather than being dropped.
* Dev12 adds center-of-mass Force/Impulse/VelocityChange/Acceleration commands
  for nonplayer bodies, runtime mass/gravity setters and fixed-step force
  queues. Force conversion uses mass at the call; subsequent velocity writes
  retain queued forces. Disable clears forces while preserving cached velocity;
  a motion-mode change clears both. All 56 measured Unity 2022.3 cases match
  with 0 and 2 solver workers. Torque, off-center forces, collision callbacks
  and the actual weapon/AI systems remain pending.
* Dev13 adds the revolver input/cooldown calculation layer for all three
  variations and alternate versions. Its Update branches and event ordering
  match 240,000 cases against the unmodified archived r52 generated method.
  Shoot, ThrowCoin, CheckCoinCharges and engine effects are recorded callee
  boundaries in this comparison. Animation readiness remains an external
  callback; it is not replaced with a made-up cooldown. The native gameplay
  library builds this module, but the development session does not yet bind
  weapon models, animation events, beam/damage processing or audio to it.
  This is not playable native combat.
* Weapon resource conversion resolves the original GunSetter GUIDs through the
  actual Addressables catalog. All 24 authored weapon prefabs retain 919
  GameObjects and their component/hierarchy fields, including the six revolvers.
  A linked animation snapshot preserves eight controllers, 46 clips, original
  curves and 36 callbacks. Neither snapshot executes scripts or replaces the
  still-pending native prefab/Animator/skinning implementation.
* GPU readback on X (v2 also captured the first frame). It saves a PPM capture and records
  non-background pixel count. A draw counter is not treated as proof of valid
  pixels. Runtime log: `/ultrakill-native.log`; capture: `/ultrakill-native.ppm`.
* Offline preservation of 58,182 authored object/component records, including
  13,567 transforms and references to 348 script classes. This migration data
  does not implement those scripts or instantiate physics bodies at runtime.
* A separate hierarchy pack retains all 13,567 objects and 44,607 owned
  components, including inactive rooms. Native TRS/world transforms, inherited
  activity, enabled components, layer state and local-preserving reparenting
  pass host checks. Player/ground position bindings run in dev6. Script names
  retain their assembly identity; component fields remain in the snapshot and
  typed system packs. The hierarchy does not execute original scene scripts.
* A host-tested fixed simulation clock with retained backlog, pause and time
  scaling, connected to the native development session.
* Native `NewMovement.Move` calculations ported from TEST16: grounded motion,
  slope idle gravity, friction, ground pushes, air acceleration and wind,
  fake-fall forces and related movement counters. Stamina recharge rules are
  ported separately. Dev3 connects this to the capsule, authored ground sensors
  and native physics, with render interpolation on a separate fixed clock.
  The rest of the player controller still needs porting; this is not complete
  gameplay. No full-game FPS claim follows from this.
* An independent physics pack/solver adapter with static collision geometry,
  player capsule, fixed simulation, materials/layers, rays and capsule overlap
  queries. It builds for Switch and passes host checks on the actual scene;
  runtime integration and solver parity remain incomplete. See
  [physics migration](physics/README.md) for exact coverage and limitations.
* GroundCheck contact bookkeeping, UpdateState timer/event logic and group
  aggregation. Engine damage, water, portal queries, moving-support parenting
  and jump/effect callbacks remain pending. An unsupported callback in the
  development session fails explicitly rather than silently pretending to run.
* A jump calculation module covers normal, surface, slow, slide, dash/enemy,
  super jumps and the jump-specific SSJ timing/velocity cap. It emits explicit
  audio/animation/slide events. The ordinary jump is connected to B in dev4;
  real-scene host tests verify lift-off, landing and latched presses at 20–240 Hz.
  Comparison with the archived original Jump body passes 120,000 cases. Its
  StopSlide/TrySSJ callees are recorded boundaries in that comparison. A separate
  original StopSlide comparison passes 60,000 cases; TrySSJ internals still need
  their comparison. Effect backends remain pending.
* Native TryDash/Dodge calculations, including stamina, surface restrictions,
  dash duration/speed, slide friction and steering. Comparisons with the actual
  archived bodies pass 100,000 TryDash and 150,000 Dodge cases. StopSlide and
  effect internals are excluded. Dev4 connects ordinary dash to L and verifies
  collision-layer restoration on the actual level in host simulation.
  Dev5 also connects ordinary slide on R, shrinking/restoring the capsule,
  shifting its authored ground probe, overhead obstruction checks and slide
  jumps. Host actual-level tests pass; full portal, wall, camera and effect
  lifecycle remains incomplete. StartSlide/HandleSlideState still require
  original-method differential comparisons.
* Static physics supports lazy activation, cached body reuse and contact-cache
  wakeup batched per simulation step. All 1,887 authored static collider shapes
  create on the host; restoring the original active set returns 329 bodies.
  The scene activation router handles static collider changes transactionally,
  coalesces their final state and retains requests for unimplemented systems.
  Audio/gameplay/rendering, moved-static and dynamic-body backends are not
  silently counted as handled. Script-driven full scene activation is incomplete.

Hardware check of geometry backend v1 loaded the scene but timed out in the
combined draw/readback submission. Backend v2 fixes the RGBA8 copy stride
(zero requests tight packing), explicitly flushes before CPU fence waits and
checks graphics completion separately from image download. It also adds the
fragment barrier before depth discard. V2 passed a console run: draw and copy
fences returned success, the queue had no error, a 1280x720 capture was saved,
and shutdown completed normally. Free-camera geometry inspection ran at about
60 FPS after the initial synchronous capture. This does not measure gameplay.
The dev7 integration builds successfully and passes host movement/physics/ground,
jump/dash/slide/posture, hierarchy and actual-scene session checks. It has not been executed
on Switch yet.

Dev7 corrects the native export of static batches with a null root: those
vertices are already in world space. The previous converter incorrectly
transformed them again. A pinned original mesh/collider comparison now matches
their world bounds exactly. The corrected export has 29,579 vertices, 18,519
triangles and 106 chunks (22 initially visible), replacing the earlier misplaced
geometry. The prior v2 console run proved GPU submission/readback, not spatial
accuracy of that export; the corrected geometry remains console-unverified.

## Migration baseline

The product being migrated is the existing TEST16/r52 game, including its
Switch fixes and settings, rather than a new game with simplified mechanics.
The `native/` executable is still an internal development stage. Existing
Unity gameplay code and authored data are the reference for native behavior;
extracting assets or generating a class stub does not count as porting it.

Migration must preserve movement and timing, weapons, enemies, level scripts,
menus and terminal actions, touch and controller/gyro input, audio, save data,
and the existing user settings. Unity object lifecycle, physics, animation,
asset loading and rendering require actual native implementations or adapted
libraries. Generated IL2CPP C++ still depends on the Unity player and cannot
become standalone merely by moving it into this directory.

The current converted scene contains 361 enabled static renderers, 29,579
vertices, 18,519 triangles and 106 spatial chunks. Inactive, dynamic/animated,
camera-excluded and shadows-only objects are excluded from this geometry-only
format. Source level counts include 1,450 rigidbodies and 956 CharacterJoints;
these are serialized objects, **not measured simultaneous active bodies**.

## Not implemented / acceptance gates

Original shader/material/texture parity; complete scene hierarchy and scripted
activation; complete native character controller and collision parity; weapons and enemy AI;
animation/skinning and ragdolls; audio; menus/touch/tutorial prompts; saves;
portals, terminals, level transitions, videos and accessibility settings remain
to be ported. Preserve the original game's credits and resource provenance.

Native GPU output, controls and shutdown require verification on actual Switch
hardware. Geometry-only FPS cannot establish full-game performance. Complete
native migration is accepted only after all required gameplay systems and
levels work, saves remain compatible or are explicitly migrated, and console
combat/transition/audio/input tests pass. Stable 60 FPS is not established.

## Movement module validation

`source/nx_movement.cpp` takes physics observations and returns velocity writes,
queued velocity changes/forces and gravity state. Queued forces are deliberately
separate from the velocity property, preserving the ordering of Unity script
reads before simulation. Ground contact and surface push rotation are supplied
by the adapter; this module does not approximate them using visible triangles.
Caller inputs must come from a valid physics body (positive finite mass and
finite timestep/vectors).

The host comparison extracts the actual Move body from the archived r52
`Assembly-CSharp__21.cpp`, preserving its control flow and constants. Engine
endpoints are mocked; comparison covers 114,000 boundary/random inputs and
checks velocity, target, aerial delta, forces, layer and jump counters. It also
checks authored walking speed and stamina rules. This verifies the calculation
boundary, not the real physics solver or complete player behavior. The original
game method remains in the user's external build directory.

```sh
python native/tools/check_movement.py --cpp LOCAL_R52/Assembly-CSharp__21.cpp --compiler PATH/g++ --output BUILD/movement-check
python native/tools/check_jump.py --cpp LOCAL_R52/Assembly-CSharp__21.cpp --compiler PATH/g++ --output BUILD/movement-check
python native/tools/check_dash.py --cpp LOCAL_R52/Assembly-CSharp__21.cpp --compiler PATH/g++ --output BUILD/movement-check
make -C BUILD -f SOURCE/native/Makefile SRCROOT=SOURCE/native gameplay
```

The make command builds the standalone movement library for Switch. The full
development app instead links CMake's physics/gameplay libraries. Next player
integration gates include the complete slide/crouch capsule lifecycle, wall and
enemy contacts, slam, callbacks and full Update/FixedUpdate order parity.

## Build

Requires the installed devkitPro Switch SDK (libnx, deko3d, UAM), Python and
UnityPy. Convert resources from the user's own game files:

```powershell
python native/tools/export_scene.py --bundle PATH/campaign_scenes_level1-2.bundle --models-bundle PATH/models.bundle --output BUILD/romfs/level1-2.nxscene
```

Build outside the source directory, supplying the native source root to make:

```sh
make -C BUILD -f SOURCE/native/Makefile SRCROOT=SOURCE/native -j4 all
```

For dev13, generate the graph pack alongside the physics pack, then generate
the owned render pack using `tools/export_render.py` with the original scene,
models bundle and `--graph BUILD/migration/level1-2.nxgraph`. Then run
`tools/export_materials.py --bundle ORIGINAL_SCENE --render BUILD/migration/level1-2.nxrender --output BUILD/migration/level1-2.nxmat`.
The material sidecar retains original shader properties and exact 64-bit
references. Native RGBA8 textures preserve source row order, wrap and base-level
filtering; authored mip chains and full shader features still need migration.
Generate the actor pack with `tools/export_actors.py --world BUILD/migration/level1-2.world.json.gz --graph BUILD/migration/level1-2.nxgraph --physics BUILD/migration/level1-2.nxphys --output BUILD/migration/level1-2.nxactor`.
Build
the Jolt/physics/gameplay dependency libraries before make.
Generate the joint pack after the actor pack with
`tools/export_joints.py --world BUILD/migration/level1-2.world.json.gz --actors BUILD/migration/level1-2.nxactor --graph BUILD/migration/level1-2.nxgraph --output BUILD/migration/level1-2.nxjoint`.
`python native/tools/export_graph.py --world BUILD/migration/level1-2.world.json.gz --output BUILD/migration/level1-2.nxgraph`
produces a separate `.graph.json` coverage report.
`tools/build_native.py` performs both stages with explicit SDK/tool paths and
records an internal-build manifest. It does not create a release NSP, publish
anything or write the SD card.
Weapon data is converted separately:

```sh
python native/tools/export_weapon_prefabs.py --world BUILD/migration/level1-2.world.json.gz --catalog GAME/assets/aa/catalog.json --output BUILD/migration/weapons.prefabs.json.gz
python native/tools/export_weapon_animations.py --weapons BUILD/migration/weapons.prefabs.json.gz --bundles GAME/assets/aa/StandaloneWindows64 --output BUILD/migration/weapons.animations.json.gz
```

These retain exact serialized-file/path identities and dependency hashes.
They are conversion inputs, not yet runtime packs consumed by the NRO.
The catalog reader does not execute managed types or scan the PC for missing
asset dependencies; bundle/CAB resolution stays explicit.
Host validation through `tools/build_physics.py` accepts `--pack`, `--graph`
and `--render`, plus `--materials` for resource binding checks; supplying the owned render pack also requires the matching
graph. This checks CPU draw selection as well as the ordinary player/physics
session, and does not substitute for console GPU or full-game testing.
Add `--actors BUILD/migration/level1-2.nxactor` to validate actor ownership,
compound queries/dynamics, graph bindings and the player session with those
initial actor bodies attached.
Add `--joints BUILD/migration/level1-2.nxjoint` to check native constraints,
activation/motion-mode changes, source identity, break events and tiny/frozen
shape behavior. The optional `nx_actor_audit ACTORS JOINTS PHYSICS GRAPH REPORT`
executable force-activates source bodies and connects joints individually;
it does not execute original game scripts or prove console performance.
`--revolver-reference LOCAL_R52/Assembly-CSharp__26.cpp` also runs the archived
Revolver.Update comparison using the supplied host compiler. Callee and engine
effect boundaries are listed in its validation report.

Dev6 development controls: left stick moves the player capsule, right stick
looks, Minus switches to the inspection camera and pauses simulation, Y resets
the development session, X captures, + exits. B jumps and L dashes in the player
view; holding R slides. L/R vertical movement and left-stick speed apply to the
inspection camera. Weapon input is not connected yet. Audio/animation/rumble requests
are logged explicitly, awaiting their real backends. These are not the final
game's complete controls.

Host checks compile `tests/scene_test.cpp` against `include/` and run it with
the converted scene path. Run `python tests/test_transforms.py` for hierarchy
and inverse-transpose normal tests. These do not validate Switch GPU execution.

deko3d API and toolchain references: [devkitPro/deko3d](https://github.com/devkitPro/deko3d),
[official Switch examples](https://github.com/switchbrew/switch-examples).
