// Editor-only correctness probe. Does not enter Play Mode or load game scenes.
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using NxPort;

public static class NxPhysicsQueryProbe
{
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    static string Snapshot(Collider[] results)
    {
        int count = Physics.OverlapBoxNonAlloc(new Vector3(15, 0, 0), new Vector3(30, 3, 3), results);
        var ids = new List<int>();
        for (int i = 0; i < count; i++) ids.Add(results[i].GetInstanceID());
        ids.Sort();
        string value = string.Join(",", ids);
        RaycastHit hit;
        if (Physics.Raycast(new Vector3(-5, 0, 0), Vector3.right, out hit, 100))
            value += ":" + hit.collider.GetInstanceID() + ":" + hit.distance.ToString("R");
        foreach (var id in results)
            if (id) value += ":" + id.bounds.center.ToString("R");
        return value;
    }
    public static void Run()
    {
        bool original = Physics.autoSyncTransforms;
        var objects = new List<GameObject>();
        try
        {
            UnityEditor.SceneManagement.EditorSceneManager.NewScene(
                UnityEditor.SceneManagement.NewSceneSetup.EmptyScene,
                UnityEditor.SceneManagement.NewSceneMode.Single);
            Physics.autoSyncTransforms = true;
            var random = new System.Random(51);
            for (int i = 0; i < 32; i++)
            {
                var go = new GameObject("NX physics probe " + i);
                go.AddComponent<BoxCollider>(); objects.Add(go);
            }
            var results = new Collider[128];
            for (int trial = 0; trial < 100; trial++)
            {
                foreach (var go in objects)
                    go.transform.position = new Vector3((float)random.NextDouble()*40, (float)random.NextDouble() - 0.5f, 0);
                string baseline = Snapshot(results);
                bool restore = NxPhysicsQueries.BeginAimRead();
                try
                {
                    Check(restore && !Physics.autoSyncTransforms, "batch policy");
                    for (int query = 0; query < 5; query++)
                        Check(Snapshot(results) == baseline, "ray/overlap/bounds differ in read batch");
                }
                finally { NxPhysicsQueries.EndAimRead(restore); }
                Check(Physics.autoSyncTransforms, "policy not restored");
            }
            // Transforms changed AFTER the batch still synchronize normally.
            foreach (var go in objects) go.transform.position += Vector3.up * 100;
            Check(Physics.OverlapBoxNonAlloc(Vector3.zero, Vector3.one * 50, results) == 0, "stale colliders after batch");
            Physics.autoSyncTransforms = false;
            Check(!NxPhysicsQueries.BeginAimRead(), "original manual policy changed");
            NxPhysicsQueries.EndAimRead(false);
            Check(!Physics.autoSyncTransforms, "original manual policy lost");
            Debug.Log("[nx-physics-probe] PASS: 32 moving colliders, 100 poses, 500 batched ray/overlap/bounds snapshots identical; post-batch moves and original sync policy retained. Editor API correctness only, not Switch FPS.");
        }
        finally
        {
            Physics.autoSyncTransforms = original;
            foreach (var go in objects) UnityEngine.Object.DestroyImmediate(go);
        }
    }
}
