using UnityEngine;

namespace NxPort
{
    // Use the game's own visual options. Apply once, then leave them editable
    // in the normal graphics menu; do not fight the player's settings each frame.
    [DefaultExecutionOrder(-30000)]
    public sealed class NxPerformance : MonoBehaviour
    {
        void Awake()
        {
            // Auto-created persistent singletons have DontSave flags and are
            // excluded from FindObjectOfType. Never scan the scene in Update:
            // a failed search used to scan every live object on every frame.
            enabled = false;
            try { Apply(MonoSingleton<PrefsManager>.Instance); }
            catch (System.Exception error)
            { NxBridge.Log("[visual-budget] initialization failed; no retry loop: " + error.Message); }
        }
        void Apply(PrefsManager prefs)
        {
            if (!prefs) { NxBridge.Log("[visual-budget] preferences unavailable; no retry loop"); return; }
            const string migration = "nxVisualBudgetV1";
            if (!prefs.GetBoolLocal(migration, false))
            {
                prefs.SetBoolLocal("simpleExplosions", true);
                prefs.SetBoolLocal("simpleFire", true);
                prefs.SetBoolLocal("disableEnvironmentParticles", true);
                prefs.SetBoolLocal("freezeGore", true);
                prefs.SetFloatLocal("maxGore", Mathf.Min(prefs.GetFloatLocal("maxGore", 3000), 100));
                prefs.SetFloatLocal("bloodStainMax", Mathf.Min(prefs.GetFloatLocal("bloodStainMax", 100000), 512));
                prefs.SetFloatLocal("bloodStainChance", Mathf.Min(prefs.GetFloatLocal("bloodStainChance", 0.5f), 0.2f));
                prefs.SetBoolLocal(migration, true);
                NxBridge.Log("[visual-budget] applied: simple fire/explosions, no environment particles, frozen gore; gore<=100 stains<=512 chance<=0.2; editable in graphics options");
            }
            else NxBridge.Log("[visual-budget] existing profile retained; no per-frame work");
            if (NxBridge.ConfigInt("performance_mode") != 0 && !prefs.GetBoolLocal("nxVisualBudgetV2", false))
            {
                // This is the game's cosmetic gore limit, not enemy count or
                // simulation frequency. Keep it editable after the migration.
                prefs.SetFloatLocal("maxGore", Mathf.Min(prefs.GetFloatLocal("maxGore", 100), 48));
                prefs.SetBoolLocal("nxVisualBudgetV2", true);
                NxBridge.Log("[visual-budget] V2: cosmetic gore<=48; physics timestep and movement unchanged");
            }
        }
    }
}
