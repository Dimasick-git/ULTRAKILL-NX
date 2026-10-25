using System;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class NxSpawnWarmupProbe
{
    public static void RunThenExport()
    {
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        Material material = null;
        ShaderVariantCollection variants = null;
        try
        {
            var root = new GameObject("NX inactive enemy warmup probe");
            SceneManager.MoveGameObjectToScene(root, scene);
            root.SetActive(false);


            var shader = Shader.Find("Unlit/Color");
            if (!shader || !shader.isSupported) throw new Exception("Warmup fixture shader unavailable");
            material = new Material(shader);
            root.AddComponent<MeshRenderer>().sharedMaterials = new[] {material, material};
            variants = new ShaderVariantCollection();
            if (NxPort.NxSpawnWarmup.AddVariants(variants, material) == 0)
                throw new Exception("No variant accepted");
            int count = variants.variantCount;
            NxPort.NxSpawnWarmup.AddVariants(variants, material);
            if (variants.variantCount != count) throw new Exception("Duplicate variant added");
            variants.WarmUp();
            if (!variants.isWarmedUp) throw new Exception("Warmup did not complete");
            int objects = scene.rootCount;
            NxPort.NxSpawnWarmup.Prewarm(scene);
            if (root.activeSelf || root.activeInHierarchy || scene.rootCount != objects)
                throw new Exception("Fixture state changed by warmup");
            if (typeof(NxPort.NxSpawnWarmup).GetMethod("Update",
                System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public |
                System.Reflection.BindingFlags.NonPublic) != null)
                throw new Exception("Warmup must not run every frame");
            Debug.Log("[spawn-warmup-probe] PASS: actual variants warm, duplicates ignored, inactive renderer/scene unchanged, no Update callback");
        }
        finally
        {
            if (variants) UnityEngine.Object.DestroyImmediate(variants);
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            if (material) UnityEngine.Object.DestroyImmediate(material);
        }
        NxBuild.AndroidExport();
    }
}
