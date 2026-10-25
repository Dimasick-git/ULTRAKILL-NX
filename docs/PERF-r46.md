# r46 / 2.0.0 TEST10

## Evidence from TEST9

`uk-build/r45-transition-feedback/debug.log`, latest boot at line 61932.
The native AMD graph completed reset frame 2186 in 71.55 ms and history
frame 2187 in 125.29 ms. It then triggered the two-frame budget fallback
at 123.97 ms. `present_extra=1` meant eligible, not proof that a swap
succeeded. This build adds explicit successful generated/real swap counters.
There was no sustained frame generation in that run.

Transition crash report: `01792528919_01005ce3b1a40000.log`, Data Abort
2168-0002, render thread 0x2a9. Exact r45 ELF resolves native +0x42a004
to nouveau_bo_ref, then nouveau_mm_allocate / nouveau_buffer_create /
nvc0_resource_create / st_bufferobj_data / buffer_data_error. A slab's BO
pointer is invalid. This does not establish which writer corrupted it.
It is distinct from r44's deko allocation abort. No pointer-skipping patch
was added to the driver.

## Changes

* Once per save/settings migration, apply the game's own visual preferences:
  simple fire/explosions, disable environmental particles, freeze decorative
  gore, maxGore <=100, bloodStainMax <=512, bloodStainChance <=0.2.
  Original defaults in the recovered DLL were 3000 gore /100000 stains /0.5.
  Values remain editable in the game's graphics settings; no recurring clamp.
  Existing lower limits are retained. Blood healing, enemy AI, hitstop,
  movement and fixed timestep 0.008 are unchanged.
* Three independent deko input uploads use one submission/fence instead
  of three. Dependencies between SDK passes retain their existing barriers.
* Log capture/upload/optical-flow/prepare/interpolation/output wall times
  separately. These are synchronized stage timings, not pure GPU counters.
* Count generated and real successful EGL swaps, including a session summary.
* Check native availability before allocating four Unity RTs or enabling
  motion-vector capture after the process-wide budget/fault fallback.
* Before releasing a session, fence all GLES commands preceding release,
  including capture blits on frames that did not enqueue an FG job. On
  unknown completion, retain the complete session and block new FG sessions.
  This strengthens teardown; the Mesa corruption root cause remains unproven.
* Add CPU scopes for BloodsplatterManager.Update, GoreZone.SlowUpdate and
  Flammable.Update to distinguish effect processing from the render path.

## Validation boundary

Native build, Unity export, patched IL2CPP build, import audit and package
verification are PC checks. They do not establish Switch FPS or successful
level transitions. FG is still experimental and retains the automatic
performance fallback. No claim of stable 60 FPS or fixed Mesa corruption.

No Git commits/pushes, no publishing, no game copy to SD. Installer is for
manual DBI installation. The old TEST9 installer is retained for comparison.
