# Native physics migration

This is a standalone C++ solver adapter, not a completed game engine or port.
It uses the public MIT-licensed Jolt source at commit
`01adafba08bddb650046a822983c52aa734fcadb`. The dependency lives outside the
repository; `tools/patch_jolt_switch.py` applies the small reproducible libnx
platform changes (platform detection, breakpoint, alloca declaration and
aligned allocation through memalign). It uses no proprietary console files.

## Current implementation

* `export_physics.py` exports authored colliders independently of renderer
  visibility: inactive records, trigger flags, layers, rigidbody ownership,
  primitive parameters, world transforms and collision mesh geometry survive.
* Player mass, capsule dimensions/offset, initial orientation/position,
  movement parameters and gravity come from the existing game data. The
  symmetric 32-layer collision matrix and material friction/restitution and
  combine modes are retained.
  Physics pack version 3 also resolves the original GroundCheckGroup instance
  references to the ground/slope capsule colliders; no scene object IDs are
  hardcoded in the C++ adapter.
  Floor/Slippery/Moving tags are resolved from the source game's TagManager,
  and CustomGroundProperties are bound to the collider or attached rigidbody
  owner. Relative surface push uses quaternion rotation, excluding scale.
* `nx_physics_pack.h` validates byte length, budgets, CRC, ranges, references,
  finite inputs, layer symmetry and player configuration before solver setup.
  Loading and world replacement are transactional.
* The adapter creates active static box/mesh/sphere/capsule bodies and sensors.
  Convex hulls and meshes use the authored collision mesh, not render batches.
  Round colliders with unsupported shear fail explicitly.
  Initially inactive static bodies can be created lazily, removed and re-added
  without allocating duplicate shapes. The adapter owns the source data needed
  for later creation. All 1,887 static definitions in level 1-2 pass shape creation
  on the host, including initially inactive rooms. Contact-cache/wakeup work
  is batched once per simulation step.
* `export_actors.py` retains 1,450 rigid bodies, original motion/mass/damping/
  constraints and 1,715 collider ownership references. Relative child activity
  survives even when a whole body starts inactive. Source graph/physics hashes
  and exact ownership are checked before attachment.
  Bodies use compound collision shapes with per-child layer/material/ID
  handling. Separate sensor shapes follow dynamic owners. Lazy activation
  reuses cached bodies. Initial attachment failure rolls back new bodies and
  metadata without replacing the existing physics world.
* `ActorScene` routes body activity from the hierarchy, supplies kinematic
  targets and writes dynamic poses back after each fixed step. Only the active
  body list is visited per tick. World poses preserve quaternion rotation
  under rotated/nonuniform parents. Geometry or layer changes requiring shape
  rebuilding are detected before simulation. Explicit center/inertia and full
  original trigger/script lifecycle remain incomplete. Runtime kinematic/dynamic
  changes and fully frozen logical dynamic bodies are supported; the latter
  use an immovable solver representation and reject velocity immediately,
  matching the Unity 2022.3 reference check.
* `export_joints.py` retains all 986 original Character/Fixed/Configurable
  joints and their actor/graph identities. Native constraints preserve initial
  frames through body reuse and motion-mode changes, lazily connect active
  owners, suppress connected-body collisions, and emit source-ID break events.
  Linear projection is supported; full PhysX projection parity is unverified.
  Soft limits, drives, mass scaling and angular projection below 180 degrees
  fail explicitly. All configurations in this source scene pass the supported
  subset check. Unity 2022.3 Editor reference runs cover twist signs and 119
  zero-secondary-axis source frames; this does not prove full solver parity.
* Orthogonal boxes use exact box shapes with zero convex radius. Half extents
  clamp to FLT_EPSILON, as measured in Unity 2022.3, including the three authored
  zero-scale hand boxes. Submicron overlap queries use analytic segment/box
  distance when generic convex-query tolerances lose these small shapes.
* The player has a dynamic capsule with locked rotation, authored mass/center,
  zero linear damping and continuous collision detection. ForceMode-style
  forces and velocity changes remain queued until the 8 ms simulation step;
  script velocity writes remain immediate. Frame rate does not set the step.
* Dev12 adds the four center-of-mass force modes for nonplayer actors and
  runtime mass/gravity setters. Force/Impulse conversion captures mass at call
  time. Queued forces survive velocity writes, clear on disable/motion-mode
  changes and apply once at the next fixed tick. Cached linear/angular velocity
  survives body removal/re-add; mode changes clear it. A measured Unity 2022.3
  reference covers 56 mass/mode/lifecycle cases with 0 and 2 Jolt workers.
  This does not implement torque, off-center forces or combat callbacks.
* Ray and capsule/sphere overlap queries support layer/trigger filters and
  ignore the player's solid body. Overlap results contain one entry per
  collider, with a bounded result budget and explicit overflow failure.
  Ground sensor queries use the authored offset, orientation and nonuniform
  scale. The separate ForceGroundCheck path retains the original unscaled
  capsule dimensions and gravity axis. Raw overlaps do not implement tag,
  portal or landing callbacks by themselves.
* Zero-worker and two-worker job configurations build and pass host checks.
  The Switch build links the complete adapter, including its thread path,
  into an internal ELF. Execution of that path on Switch is **unverified**.

For the current 1-2 scene the pack retains 3,602 colliders and 1,162 distinct
collision meshes. The adapter instantiates 329 static bodies, including 22
sensors, plus the player capsule. Dev10 additionally creates the four original
active kinematic bodies: three checkpoint trigger owners and ParticleTrigger.
They contain eleven active colliders. Five player helper colliders remain
deferred. Inactive bodies remain available for later scripted activation;
Dev11 loads 986 authored joint definitions. A separate host audit successfully
creates all 1,449 nonplayer authored bodies and attaches each of the 986 joints
individually, with no creation errors. This forces inactive source data on;
it is not a scene gameplay or animated ragdoll validation.

## Validation and remaining gates

Host checks cover capsule geometry and floor contacts, layer and trigger
filtering, ordinary and zero-length capsule overlap queries, deferred forces,
CCD at 1,000 units/s, two worker settings and identical 125 Hz simulation at
20/30/60/120 rendering rates. Corrupt-pack tests cover lengths, allocation
budgets, checksums and malformed configuration. The actual scene runs 1,000
ticks without nonfinite state. Host timings are not Switch performance results.

This has not established solver parity with Unity/PhysX. In particular, original
ground-check trigger lifecycle, portals, moving-body parenting, collision
callbacks, character events, distinct static/dynamic friction, per-body solver
settings, complete animated ragdolls, runtime compound rebuilding and full actor lifecycle still need implementation and
comparison. Ground checks must use the original sensor definitions and state
rules, not a replacement floor ray. Dev3 connects the native session to the
renderer and controller: ground sensor overlaps -> contact/state bookkeeping ->
ported Move calculations -> solver -> interpolated camera. Physics is paused
in inspection mode and while unfocused; resume/capture timing does not enqueue
the elapsed pause as simulation backlog. Full player methods and scene events
remain incomplete, and the integrated executable is not hardware verified.

## Reproduce

Use the user's local source bundle, world export, scene report and recovered
Unity physics settings. All generated game data stays in the build directory:

```sh
python native/tools/export_physics.py --world BUILD/level1-2.world.json.gz --bundle GAME/campaign_scenes_level1-2.bundle --scene-meta BUILD/level1-2.json --settings PROJECT/ProjectSettings/DynamicsManager.asset --output BUILD/level1-2.nxphys
python native/tools/build_physics.py --jolt EXTERNAL/JoltPhysics --output BUILD/physics-host --cmake PATH/cmake --ninja PATH/ninja --host-compiler PATH/g++ --pack BUILD/level1-2.nxphys
python native/tools/build_physics.py --jolt EXTERNAL/JoltPhysics --output BUILD/physics-switch --cmake PATH/cmake --ninja PATH/ninja --switch --devkitpro C:/devkitPro
```

An isolated UnityPy environment can be supplied with `--python-lib` to the
exporter. The build helper stops on every failed command and records artifact
hashes and validation scope. `nx_physics_link.elf` is an internal CPU check;
it is not a playable NRO or an NSP release.
`--force-reference PATH/force-oracle-unity.json` additionally compares the host
force adapter with saved measurements from `tests/ForceOracle.cs`, run in an
isolated Unity 2022.3.29f1 Editor project. Unity is not a native app dependency.

Upstream: [Jolt Physics](https://github.com/jrouwe/JoltPhysics).
License notice: [JoltPhysics.txt](../licenses/JoltPhysics.txt).
