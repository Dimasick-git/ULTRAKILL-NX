using System;
using System.Linq;
using UnityEngine;

public static class NxVisualBudgetProbe
{
    public static void RunThenExport()
    {
        Run();
        NxBuild.AndroidExport();
    }

    public static void Run()
    {
        var go = new GameObject("NX hidden lookup regression probe");
        try
        {
            var marker = go.AddComponent<Light>();
            if (!UnityEngine.Object.FindObjectsOfType<Light>().Contains(marker))
                throw new Exception("Visible marker missing before test");
            // Match the recovered MonoSingleton exactly: flags are placed on
            // its GameObject, not the component; value 20, not the full 52.
            go.hideFlags = HideFlags.DontSaveInEditor | HideFlags.DontSaveInBuild;
            if (UnityEngine.Object.FindObjectsOfType<Light>().Contains(marker))
                throw new Exception("Hidden marker unexpectedly discoverable");
            if (!Resources.FindObjectsOfTypeAll<Light>().Contains(marker))
                throw new Exception("Hidden marker must remain alive");
            var type = typeof(NxPort.NxPerformance);
            if (type.GetMethod("Update", System.Reflection.BindingFlags.Instance |
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public) != null)
                throw new Exception("Settings component must have no per-frame callback");
            Debug.Log("[visual-budget-probe] PASS: live component on GameObject flags=20 excluded from scene lookup; production component has no Update callback");
        }
        finally { UnityEngine.Object.DestroyImmediate(go); }
    }
}
