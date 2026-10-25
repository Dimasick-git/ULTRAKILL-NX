using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Profiling;
using UnityEngine.SceneManagement;

namespace NxPort
{
    public static class NxSceneMemory
    {
        // Called only from SceneHelper's full scene-load coroutine. Its first
        // yield has already stopped gameplay and displayed the loading blocker.
        public static IEnumerator WrapSceneLoad(IEnumerator original, Component owner)
            => Sequence(original, NxBridge.Available ? () => ReleasePreviousScene(owner) : null);

        public static IEnumerator Sequence(IEnumerator original, Func<IEnumerator> cleanup)
        {
            try
            {
                if (!original.MoveNext()) yield break; // rejected duplicate request
                yield return original.Current;
                if (cleanup != null) yield return cleanup();
                while (original.MoveNext()) yield return original.Current;
            }
            finally { (original as IDisposable)?.Dispose(); }
        }

        // Observe completion ourselves rather than yielding the native operation
        // directly. The scene-unloaded event is also authoritative if a platform
        // coroutine wait token stops advancing after the scene has gone away.
        public static IEnumerator UnloadScene(Scene scene)
        {
            bool unloaded = false;
            string name = scene.name;
            void Finished(Scene value) { if (value.handle == scene.handle) unloaded = true; }
            SceneManager.sceneUnloaded += Finished;
            try
            {
                var operation = SceneManager.UnloadSceneAsync(scene);
                float started = Time.realtimeSinceStartup, report = started + 5;
                while (operation != null && !operation.isDone && !unloaded)
                {
                    if (Time.realtimeSinceStartup >= report)
                    {
                        NxBridge.Log("[memory] unload pending: " + name + " loaded="
                            + (scene.IsValid() && scene.isLoaded) + " progress=" + operation.progress
                            + " elapsed=" + (Time.realtimeSinceStartup - started));
                        report = Time.realtimeSinceStartup + 5;
                    }
                    yield return null;
                }
                NxBridge.Log("[memory] unload complete: " + name + " event=" + unloaded
                    + " operation=" + (operation == null || operation.isDone));
            }
            finally { SceneManager.sceneUnloaded -= Finished; }
        }

        public static IEnumerator ReleasePreviousScene(Component owner)
        {
            long before = Profiler.GetTotalAllocatedMemoryLong();
            NxBridge.Loading(true);
            // The persistent loader and its loading canvas must outlive cleanup.
            // Leave startup alone if it has not yet moved into DontDestroyOnLoad.
            if (owner && owner.gameObject.scene.name == "DontDestroyOnLoad")
            {
                var previous = new List<Scene>();
                for (int i = 0; i < SceneManager.sceneCount; i++) previous.Add(SceneManager.GetSceneAt(i));
                var empty = SceneManager.GetSceneByName("NX Loading");
                if (!empty.IsValid() || !empty.isLoaded) empty = SceneManager.CreateScene("NX Loading");
                SceneManager.SetActiveScene(empty);
                foreach (var scene in previous)
                {
                    if (!scene.IsValid() || !scene.isLoaded || scene.handle == empty.handle) continue;
                    NxBridge.Log("[memory] unload before next level: " + scene.name);
                    yield return UnloadScene(scene);
                }
            }
            // Complete object destruction, then release unused native resources
            // and managed wrappers before Addressables allocates the next level.
            yield return null;
            GC.Collect();
            yield return Resources.UnloadUnusedAssets();
            GC.Collect();
            yield return null;
            uint ticket = NxBridge.MemoryTrim(true);
            float deadline = Time.realtimeSinceStartup + 10;
            while (ticket != 0 && NxBridge.MemoryTrim(false) < ticket && Time.realtimeSinceStartup < deadline)
                yield return null;
            if (ticket != 0 && NxBridge.MemoryTrim(false) < ticket)
                NxBridge.Log("[memory] render trim pending; continuing scene load");
            NxBridge.Log("[memory] pre-load cleanup: Unity allocated " + (before >> 20) + " -> "
                + (Profiler.GetTotalAllocatedMemoryLong() >> 20) + " MB");
        }
    }
}
