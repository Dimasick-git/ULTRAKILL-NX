#pragma once
/* Preserve explicit game caps. Android -1 is 30 FPS, so resolve None to
 * 1000 as before. Only the clean-install default changes to 60. */
static inline int nx_fps_resolve(int requested, int override, int overlay) {
  int fps = override > 0 ? override : requested;
  if (fps <= 0) fps = 1000;
  if (overlay >= 15 && overlay < fps) fps = overlay;
  return fps;
}

/* Remember the game's request separately from the cap we wrote. Removing an
 * overlay cap must restore the game cap without requiring a menu toggle. */
typedef struct { int initialized, requested, applied; } NxFpsPolicy;
static inline int nx_fps_observe(NxFpsPolicy *state, int observed, int override, int overlay) {
  if (!state->initialized || observed != state->applied) state->requested = observed;
  state->initialized = 1;
  state->applied = nx_fps_resolve(state->requested, override, overlay);
  return state->applied;
}

/* A menu selection is explicit even when it equals an active overlay cap. */
static inline int nx_fps_select(NxFpsPolicy *state, int requested, int override, int overlay) {
  state->initialized=1;state->requested=requested;
  state->applied=nx_fps_resolve(requested,override,overlay);
  return state->applied;
}
