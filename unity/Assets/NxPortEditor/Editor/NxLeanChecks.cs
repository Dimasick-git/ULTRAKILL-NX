using System;
using System.Reflection;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;

public static class NxLeanChecks
{
    static readonly BindingFlags PrivateInstance = BindingFlags.NonPublic | BindingFlags.Instance;
    static void Require(bool condition, string message) { if (!condition) throw new Exception("[lean-check] " + message); }
    public static void Check()
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var go = new GameObject("Lean compositor check");
        var compositor = go.AddComponent<NxPort.NxFsr>();
        var type = typeof(NxPort.NxFsr);
        try
        {
            type.GetMethod("OnEnable", PrivateInstance).Invoke(compositor, null);
            for (int i = 0; i < 100; i++) type.GetMethod("LateUpdate", PrivateInstance).Invoke(compositor, null);
            foreach (string name in new[] { "bundle", "material", "displayCamera", "low", "upscaled", "commands", "frameGeneration" })
                Require(type.GetField(name, PrivateInstance).GetValue(compositor) == null, "OFF allocated " + name);
            Require(!(bool)type.GetField("failed", PrivateInstance).GetValue(compositor), "OFF requires a shader/bundle");
        }
        finally
        {
            type.GetMethod("OnDisable", PrivateInstance).Invoke(compositor, null);
            UnityEngine.Object.DestroyImmediate(go);
        }
        var panel = new GameObject("Lean settings check", typeof(RectTransform));
        var settings = panel.AddComponent<NxPort.NxPortSettings>();
        try
        {
            var method = typeof(NxPort.NxPortSettings).GetMethod("Switch", PrivateInstance);
            method.Invoke(settings, new object[] { panel.transform, "Frame generation", "frame_generation", 0f });
            var button = panel.GetComponentInChildren<Button>();
            Require(button && !button.interactable, "unavailable FG remains interactive");
            Require(button.GetComponentInChildren<Text>().text == "OFF", "unavailable FG is shown as enabled");
        }
        finally { UnityEngine.Object.DestroyImmediate(panel); }
        Debug.Log("NX_LEAN_CHECKS_PASS: 100 disabled-filter updates allocate no FSR bundle/material/RT/camera/commands; unavailable FG button disabled. GPU/gameplay behavior still requires console validation.");
        EditorApplication.Exit(0);
    }
}
