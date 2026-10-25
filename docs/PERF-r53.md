# r53: return to the existing Unity/GLES port

The active target is the full TEST16/r52 game. The separate native C++ rewrite
is not the release path requested by the user. Its dev13 test remains separate.

## Evidence from r52

The supplied console log records deko frame generation at 60.71 and 57.14 ms
for compute plus transfer. It presents one generated frame, then falls back
because the cost exceeds its budget. Removing this backend prevents that
expensive experiment; it does not prove a sustained improvement after the
existing fallback, when generation was already inactive.

Other observed costs remain: shader compilation stalls and UnityMain load.
The largest recorded CPU scope total is CameraFrustumTargeter.Update, but
inclusive scope totals are not a complete frame-time breakdown. They must not
be presented as proof of the sole bottleneck.

## Changes

* Normal loader builds exclude all deko/AMD FG/probe translation units and the
  deko3d library. `NX_EXPERIMENTAL_FRAMEGEN=1` remains an explicit developer
  build, using a separate object directory to avoid mixed compilation flags.
* Existing managed imports remain available through a small disabled backend.
  It allocates no session or GPU resources and falls through to normal EGL
  presentation. Stale FG/probe settings are cleared on load; unavailable FG
  cannot be enabled through the settings bridge.
* The managed panel disables the unavailable FG toggle.
* With the filter off, the FSR controller no longer loads its bundle or creates
  a material. Enabling it loads once; toggling reuses the material while
  retaining existing target cleanup and scene recovery.

The game scripts, fixed timestep, combat and gameplay speed are retained.
Spatial/temporal upscaling is separate from frame generation and remains in
the source. No stable-FPS claim is made.

## Verification and remaining work

The normal ARM64 loader builds and resolves every import of the existing r52
game libraries. Its symbol table contains no deko device/queue or AMD OF/FI
dispatch implementation. Production disabled-backend ABI checks pass.

Unity Editor checks pass for 100 filter-off updates without FSR resources and
for the disabled settings button. The production compositor host test checks
off/on/off transitions, material reuse, target cleanup, scene recovery and
unsubscription. Neither check measures Switch GPU output or performance.

The updated managed sources still require Android/IL2CPP export, restoration
of all existing generated/runtime patches, full staging/import/provenance
checks, packaging and console verification. The new native loader alone is
an intermediate candidate, not an installer containing all these changes.
