using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace NxPort
{
    // Prepare only shader variants referenced by scene enemies, including
    // inactive arena waves. No enemy is enabled or simulated by this component.
    public sealed class NxSpawnWarmup : MonoBehaviour
    {
        const int VariantLimit = 96;
        readonly HashSet<int> visited = new HashSet<int>();
        void OnEnable()
        {
            SceneManager.sceneLoaded += Loaded;
            SceneManager.sceneUnloaded += Unloaded;
            for (int i = 0; i < SceneManager.sceneCount; i++)
                Loaded(SceneManager.GetSceneAt(i), LoadSceneMode.Additive);
        }
        void OnDisable()
        { SceneManager.sceneLoaded -= Loaded; SceneManager.sceneUnloaded -= Unloaded; visited.Clear(); }
        void Unloaded(Scene scene) { visited.Remove(scene.handle); }

        public static int AddVariants(ShaderVariantCollection collection, Material material)
        {
            if (!material || !material.shader || !material.shader.isSupported) return 0;
            int added = 0;
            // The game uses custom unlit and forward passes. Invalid pass /
            // keyword combinations are rejected by Unity and never expanded.
            foreach (var pass in new[] {PassType.Normal, PassType.ForwardBase, PassType.ForwardAdd})
            {
                if (collection.variantCount >= VariantLimit) break;
                try
                {
                    var variant = new ShaderVariantCollection.ShaderVariant(material.shader, pass, material.shaderKeywords);
                    if (collection.Add(variant)) added++;
                }
                catch (ArgumentException) { }
            }
            return added;
        }

        void Loaded(Scene scene, LoadSceneMode mode)
        {
            if (!scene.IsValid() || !scene.isLoaded || !visited.Add(scene.handle) ||
                NxBridge.ConfigInt("spawn_warmup") == 0) return;
            Prewarm(scene);
        }

        public static void Prewarm(Scene scene)
        {
            if (!scene.IsValid() || !scene.isLoaded) return;
            var started = Time.realtimeSinceStartupAsDouble;
            ShaderVariantCollection collection = null;
            int enemies = 0, materials = 0;
            try
            {
                collection = new ShaderVariantCollection {hideFlags = HideFlags.HideAndDontSave};
                var seen = new HashSet<Material>();
                foreach (var root in scene.GetRootGameObjects())
                foreach (var enemy in root.GetComponentsInChildren<EnemyIdentifier>(true))
                {
                    enemies++;
                    foreach (var renderer in enemy.GetComponentsInChildren<Renderer>(true))
                    foreach (var material in renderer.sharedMaterials)
                    {
                        if (!material || !seen.Add(material)) continue;
                        materials++; AddVariants(collection, material);
                        if (collection.variantCount >= VariantLimit) goto Ready;
                    }
                }
            Ready:
                if (collection.variantCount != 0) collection.WarmUp();
                if (enemies != 0)
                    NxBridge.Log($"[spawn-warmup] scene={scene.name} enemies={enemies} materials={materials} variants={collection.variantCount}/{VariantLimit} warmed={collection.isWarmedUp} elapsed={(Time.realtimeSinceStartupAsDouble-started)*1000:0.0}ms; enemy activation unchanged");
            }
            catch (Exception error) { NxBridge.Log("[spawn-warmup] skipped: " + error.Message); }
            finally
            {
                if (collection)
                {
                    if (Application.isPlaying) Destroy(collection);
                    else DestroyImmediate(collection);
                }
            }
        }
    }
}
