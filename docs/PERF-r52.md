# r52 / TEST16 — skip empty aim synchronization; stopped gore sleep

## TEST15 feedback

Full SD log/config/preferences are copied to `uk-build/r51-fps-feedback`.
`latest.log` is the r51 boot; `windows.json` pairs loop stages with matching
engine/present frame counts. Dividing physics call means by rendering FPS would
be wrong: the total physics time is divided by that loop's actual frame count.

In level 1-2, a combat window is 20.7 FPS / 48.24 ms mean / 116.28 ms p95.
Its 598 physics calls across 104 rendered frames account for 15.89 ms per frame.
Other stage elapsed times in that window: fixed scripts 3.35 ms, Update scripts
5.64 ms, LateUpdate scripts 1.79 ms, frame rendering 10.82 ms. These brackets
include native callbacks/waits and need not be treated as independent pure CPU
costs. Another window at 23 FPS spends 12.45 ms per frame in physics. Several
other intervals reach 45–60 FPS. Stable 60 is not achieved.

Registered managed exceptions remain zero in the latest boot. This is not proof
that every fault is fixed. Shader compilation/linking still produces spikes;
the existing 57-variant warmup does not cover every runtime pipeline. SaltyNX
maps successfully. Main composition is FSR OFF at 960x540; the user's settings
are retained. The physical display refresh has not been verified: the service
still returns 80 Hz.

## Changes

1. Correct the TEST15 LateUpdate sync scope. Previously its constructor always
   ran before the early return for no autoaim target. Place it inside the
   original valid-target branch immediately before the bounds read. Empty
   LateUpdate no longer forces physics synchronization. Keep the initial scan's
   scope and exception-safe restoration. Method timing still includes the work.
   The old log includes 0.834 ms LateUpdate with a 0.092 ms Update after returning
   to menu; it does not prove every such sample is solely this synchronization.
2. GoreSplatter.StopGore already zeroes velocity and disables collisions but
   leaves a dynamic body integrating gravity. Add Rigidbody.Sleep after those
   operations. Do not make bodies kinematic, disable their gravity permanently,
   or change live enemies, player movement, projectiles, solver iterations,
   fixed timestep or wave timing. Existing pool launch restores collisions and
   adds a nonzero force, waking the body. Stopped cosmetic pieces now stay in
   place until reused/removed; their persistence can change visual/render cost.
   The magnitude of the Switch benefit is unmeasured.
3. Add seven method scopes, including stopped gore, enemy Update, zombie/drone
   Update/FixedUpdate and enemy ground checks. The native name/ID table matches
   all 24 generated IDs. Restrict slow-spawn samples to the original spawn IDs;
   new gameplay scopes must not be mislabeled as spawn costs.

## Verification

Autoaim patch tests compare the exact original method body after stripping only
the added scope/include, verify idempotence and the new location after the empty
return and before bounds. Compiled production RAII tests still cover nested,
early-return and exception restoration. Gore patch tests remove only the added
sleep statement/prototype and compare the entire original generated source;
pool launch is unchanged. The patch is idempotent.

An Editor-only physics probe, without Play Mode, tests 64 dynamic bodies under
gravity for 125 steps at 0.008: sleeping bodies do not drift, the original
collision/force/rotation pool launch wakes them and restores motion/gravity,
and a second stop sleeps them again. Gravity and kinematic flags remain
unchanged. This validates API behavior in a fixture, not Switch performance or
all gameplay interactions.

Runtime C# is unchanged from the successful r51 export. Reuse that export's
metadata and compile updated generated C++: 13 successful nodes, zero failures.
Native build, stage/import checks, exact snapshots and NSP integrity are recorded
in `uk-build/current-work-r52.json` after packaging. No SD files/settings changed,
no commits/push/publication. The main renderer remains Unity GLES/Mesa; deko
frame generation is experimental. Full deko rendering, all-bugs-fixed and stable
60 FPS are not claimed.
