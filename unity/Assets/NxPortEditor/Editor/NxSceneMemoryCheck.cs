using System;
using System.Collections;
using System.Collections.Generic;
using NxPort;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

[InitializeOnLoad]
public static class NxSceneMemoryCheck
{
    static NxSceneMemoryCheck() { EditorApplication.playModeStateChanged += Started; }
    static void Require(bool value, string message) { if (!value) throw new Exception("[nx-memory] " + message); }
    public static void CheckSequence()
    {
        var events = new List<string>();
        IEnumerator Original()
        {
            try { events.Add("blocker"); yield return null; events.Add("load"); yield return "loaded"; events.Add("finish"); }
            finally { events.Add("disposed"); }
        }
        IEnumerator Cleanup() { events.Add("cleanup"); yield return null; events.Add("cleaned"); }
        var wrapped = NxSceneMemory.Sequence(Original(), Cleanup);
        Require(wrapped.MoveNext() && wrapped.Current == null, "loading blocker yield lost");
        Require(wrapped.MoveNext() && wrapped.Current is IEnumerator, "cleanup did not run before loading");
        var cleanup = (IEnumerator)wrapped.Current; while (cleanup.MoveNext()) { }
        Require(wrapped.MoveNext() && (string)wrapped.Current == "loaded", "original load continuation lost");
        Require(!wrapped.MoveNext(), "load coroutine did not complete");
        Require(string.Join(",", events) == "blocker,cleanup,cleaned,load,finish,disposed", "wrong transition ordering");
        events.Clear(); wrapped = NxSceneMemory.Sequence(Original(), Cleanup); wrapped.MoveNext();
        ((IDisposable)wrapped).Dispose(); Require(events.Contains("disposed"), "interrupted load not disposed");
        Debug.Log("[nx-memory-order] PASS: blocker -> cleanup -> next level; original yields and disposal preserved");
    }

    static readonly Stack<IEnumerator> stack = new Stack<IEnumerator>();
    static AsyncOperation waiting;
    static double deadline;
    public static void Run()
    {
        CheckSequence();
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        SessionState.SetBool("NxSceneMemoryCheck.pending", true);
        EditorApplication.EnterPlaymode();
    }
    static void Started(PlayModeStateChange state)
    {
        if (state != PlayModeStateChange.EnteredPlayMode || !SessionState.GetBool("NxSceneMemoryCheck.pending", false)) return;
        SessionState.SetBool("NxSceneMemoryCheck.pending", false);
        deadline = EditorApplication.timeSinceStartup + 90;
        stack.Push(Exercise()); EditorApplication.update += Tick;
    }
    static IEnumerator Exercise()
    {
        var owner = new GameObject("Persistent loader fixture"); UnityEngine.Object.DontDestroyOnLoad(owner);
        Time.timeScale = 0;
        for (int iteration = 0; iteration < 5; iteration++)
        {
            var level = SceneManager.CreateScene("Memory fixture repeated mission");
            var footsteps = SceneManager.CreateScene("Memory fixture repeated mission - Footsteps", new CreateSceneParameters(LocalPhysicsMode.Physics3D));
            var levelObject = new GameObject("Old level object"); SceneManager.MoveGameObjectToScene(levelObject, level);
            var stepObject = new GameObject("Old footstep object"); SceneManager.MoveGameObjectToScene(stepObject, footsteps);
            SceneManager.SetActiveScene(level);
            yield return NxSceneMemory.ReleasePreviousScene(owner.transform);
            Require(owner && !levelObject && !stepObject, "persistent loader or old scene object lifetime incorrect");
            Require(!level.isLoaded && !footsteps.isLoaded, "old scene was retained");
            Require(SceneManager.sceneCount == 1 && SceneManager.GetActiveScene().name == "NX Loading", "transition scenes accumulate");
        }
        Debug.Log("[nx-memory-scenes] PASS: five paused repeated-mission cleanup cycles with local physics; level and footsteps unloaded, persistent owner kept, one empty scene");
    }
    static void Tick()
    {
        try
        {
            if (EditorApplication.timeSinceStartup > deadline) throw new Exception("Scene cleanup test timed out");
            if (waiting != null && !waiting.isDone) return; waiting = null;
            while (stack.Count != 0)
            {
                var current = stack.Peek();
                if (!current.MoveNext()) { (current as IDisposable)?.Dispose(); stack.Pop(); continue; }
                if (current.Current is IEnumerator nested) { stack.Push(nested); continue; }
                waiting = current.Current as AsyncOperation; return;
            }
            EditorApplication.update -= Tick; EditorApplication.Exit(0);
        }
        catch (Exception error) { Debug.LogException(error); EditorApplication.update -= Tick; EditorApplication.Exit(1); }
    }
}
