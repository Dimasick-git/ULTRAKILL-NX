using System;
using System.Collections.Generic;
using System.Diagnostics;
using UnityEngine;
using UnityEngine.LowLevel;

namespace NxPort
{
    // Release-player profiler markers omit physics and script timings. Bracket
    // the existing native nodes instead; retain their pointers and ordering.
    public sealed class NxLoopTimings : MonoBehaviour
    {
        sealed class BeginMarker { }
        sealed class EndMarker { }
        static readonly string[] Wanted = {
            "FixedUpdate+ScriptRunBehaviourFixedUpdate", "FixedUpdate+PhysicsFixedUpdate",
            "Update+ScriptRunBehaviourUpdate", "PreUpdate+PhysicsUpdate", "PreUpdate+AIUpdate",
            "PreLateUpdate+AIUpdatePostScript", "PreLateUpdate+DirectorUpdateAnimationBegin",
            "PreLateUpdate+DirectorUpdateAnimationEnd", "PreLateUpdate+ScriptRunBehaviourLateUpdate",
            "PostLateUpdate+UpdateAllRenderers", "PostLateUpdate+UpdateAllSkinnedMeshes",
            "PostLateUpdate+FinishFrameRendering"
        };
        sealed class Metric
        {
            public string Name;
            public long Start, Total, Max, Calls;
            public bool Running;
            public void Begin() { Start = Stopwatch.GetTimestamp(); Running = true; }
            public void End()
            {
                if (!Running) return;
                long ticks = Stopwatch.GetTimestamp() - Start;
                Running = false; Total += ticks; Max = Math.Max(Max, ticks); Calls++;
            }
        }
        readonly List<Metric> metrics = new List<Metric>();
        float next;
        bool installed;

        static bool Own(Type type) { return type == typeof(BeginMarker) || type == typeof(EndMarker); }
        static void Strip(ref PlayerLoopSystem node)
        {
            if (node.subSystemList == null) return;
            var children = new List<PlayerLoopSystem>(node.subSystemList.Length);
            foreach (var original in node.subSystemList)
            {
                if (Own(original.type)) continue;
                var child = original; Strip(ref child); children.Add(child);
            }
            node.subSystemList = children.ToArray();
        }
        void Decorate(ref PlayerLoopSystem node)
        {
            if (node.subSystemList == null) return;
            var children = new List<PlayerLoopSystem>(node.subSystemList.Length);
            foreach (var original in node.subSystemList)
            {
                var child = original;
                string name = child.type == null ? "" : child.type.FullName;
                const string prefix = "UnityEngine.PlayerLoop.";
                if (name.StartsWith(prefix, StringComparison.Ordinal) &&
                    Array.IndexOf(Wanted, name.Substring(prefix.Length)) >= 0)
                {
                    var metric = new Metric { Name = name.Substring(prefix.Length) };
                    metrics.Add(metric);
                    children.Add(new PlayerLoopSystem {type = typeof(BeginMarker), updateDelegate = metric.Begin});
                    children.Add(child);
                    children.Add(new PlayerLoopSystem {type = typeof(EndMarker), updateDelegate = metric.End});
                }
                else { Decorate(ref child); children.Add(child); }
            }
            node.subSystemList = children.ToArray();
        }
        void OnEnable()
        {
            if (NxBridge.ConfigInt("engine_profiler") == 0) { enabled = false; return; }
            var loop = PlayerLoop.GetCurrentPlayerLoop();
            Strip(ref loop); metrics.Clear(); Decorate(ref loop);
            if (metrics.Count == 0) { NxBridge.Log("[loop-time] no supported nodes; disabled"); enabled = false; return; }
            PlayerLoop.SetPlayerLoop(loop); installed = true;
            next = Time.unscaledTime + 5;
            NxBridge.Log("[loop-time] installed=" + metrics.Count + "; original native systems retained; elapsed times include callbacks/waits, may overlap");
        }
        void Update()
        {
            if (Time.unscaledTime < next) return;
            float seconds = Time.unscaledTime - (next - 5); next = Time.unscaledTime + 5;
            foreach (var metric in metrics)
            {
                if (metric.Calls != 0)
                    NxBridge.Log($"[loop-time] {metric.Name} calls={metric.Calls} mean={metric.Total*1000.0/Stopwatch.Frequency/metric.Calls:0.000}ms max={metric.Max*1000.0/Stopwatch.Frequency:0.000}ms total={metric.Total*1000.0/Stopwatch.Frequency:0.0}ms window={seconds:0.00}s inclusive");
                metric.Total = metric.Max = metric.Calls = 0;
            }
        }
        void OnDisable()
        {
            if (installed)
            {
                var loop = PlayerLoop.GetCurrentPlayerLoop(); Strip(ref loop); PlayerLoop.SetPlayerLoop(loop);
                installed = false;
            }
            metrics.Clear();
        }
    }
}
