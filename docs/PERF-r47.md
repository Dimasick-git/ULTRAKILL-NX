# r47 / TEST11 — TEST10 settings-discovery regression

Feedback: `uk-build/r46-fps-feedback/latest.log`, last boot r46. Long level
windows show 22 FPS at ~98% UnityMain, later 10–12 FPS. No visual-budget
success message appears. LocalPrefs.json only contains the old options.

The added NxPerformance component repeatedly called
FindObjectOfType<PrefsManager>() from Update until it found the manager.
The recovered game's MonoSingleton<T>.GetOrCreateInstance adds hide flags
20 (DontSaveInEditor | DontSaveInBuild) to persistent auto-created instances.
Unity's scene lookup excludes DontSave objects:
https://docs.unity3d.com/2022.3/Documentation/ScriptReference/Object.FindObjectOfType.html

Consequently the search could never succeed. It scanned loaded objects every
frame and never disabled itself. Larger scenes made this regression worse.
This is an error introduced by TEST10, not evidence that cheaper effects
inherently slow the game.

## Correction

* Fetch the existing manager through MonoSingleton<PrefsManager>.Instance
  once in Awake, after the game's first scene initialization.
* Remove Update entirely. Disable the component before applying settings.
  Missing-manager and exception paths log once and cannot create retry loops.
* Use SetBoolLocal / SetFloatLocal to match the game's GetBoolLocal /
  GetFloatLocal graphics readers. TEST10 incorrectly used global setters.
* Store/read the migration marker in the same local preferences map. Apply
  once; subsequent boots retain the user's edits and lower existing limits.

FG in the feedback generated and successfully presented exactly one extra
frame before fallback. Measured stages in ms: capture ~40–42, uploads ~2,
optical flow ~6–7, prepare ~3, interpolation ~65, output ~9–11.
Total history-frame cost ~127ms. Its shutdown does not explain the sustained
22 FPS windows afterward. FG math, resolution, presentation and budget guard
are unchanged in TEST11 to isolate the settings regression.

## Checks

`tools/test_visual_budget.py` compiles and runs the production NxPerformance
class with separate local preferences and no scene-search API. It checks:
no Update method; one-shot initialization; local migration; lower limits;
preservation of user edits; missing-manager and injected-failure termination.

`NxVisualBudgetProbe.RunThenExport` checks in the actual Unity Editor, without
PlayMode, that a live DontSave component disappears from scene lookup while
remaining present in Resources.FindObjectsOfTypeAll. It also checks that the
production class has no Update before exporting the Android runtime.

These establish the code defect and corrected lifecycle. Switch FPS recovery
and the older Mesa transition corruption still require hardware confirmation.
No changes to physics timestep, game speed, hitstop or saves. No commit/push,
publishing, or copying the game to SD.
