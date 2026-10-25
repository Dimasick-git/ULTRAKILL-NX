using System;
using System.Collections.Generic;
using Unity.Profiling;
using Unity.Profiling.LowLevel;
using Unity.Profiling.LowLevel.Unsafe;
using UnityEngine;

namespace NxPort
{
    public sealed class NxFrameTimings : MonoBehaviour
    {
        static readonly string[] Wanted = {"Main Thread", "Physics.Simulate", "BehaviourUpdate",
            "Camera.Render", "WaitForTargetFPS", "Gfx.WaitForPresentOnGfxThread"};
        sealed class Metric
        {
            public string Name;
            public ProfilerRecorder Recorder;
            public long Total, Max;
            public int Count;
        }
        readonly List<Metric> metrics = new List<Metric>();
        bool initialized;
        float next;
        void OnEnable() { if (NxBridge.ConfigInt("engine_profiler") == 0) { enabled = false; return; } next = Time.unscaledTime + 2; }
        void Update()
        {
            try
            {
                if (!initialized)
                {
                    if (Time.unscaledTime < next) return;
                    initialized = true;
                    var available = new List<ProfilerRecorderHandle>();
                    ProfilerRecorderHandle.GetAvailable(available);
                    foreach (var handle in available)
                    {
                        var info = ProfilerRecorderHandle.GetDescription(handle);
                        if (info.UnitType != ProfilerMarkerDataUnit.TimeNanoseconds || Array.IndexOf(Wanted, info.Name) < 0) continue;
                        var recorder = new ProfilerRecorder(handle, 1,
                            ProfilerRecorderOptions.StartImmediately | ProfilerRecorderOptions.WrapAroundWhenCapacityReached |
                            ProfilerRecorderOptions.SumAllSamplesInFrame);
                        if (recorder.Valid) metrics.Add(new Metric {Name = info.Name, Recorder = recorder});
                        else recorder.Dispose();
                    }
                    NxBridge.Log("[engine-time] available=" + metrics.Count + "; aggregate marker elapsed times, may overlap/include waits; not additive CPU work");
                    if (metrics.Count == 0) { enabled = false; return; }
                    next = Time.unscaledTime + 5;
                }
                foreach (var metric in metrics)
                {
                    if (metric.Recorder.Count == 0) continue;
                    long value = metric.Recorder.LastValue;
                    metric.Total += value; metric.Max = Math.Max(metric.Max, value); metric.Count++;
                }
                if (Time.unscaledTime < next) return;
                next = Time.unscaledTime + 5;
                foreach (var metric in metrics)
                {
                    if (metric.Count != 0)
                        NxBridge.Log($"[engine-time] {metric.Name} mean={metric.Total*1e-6/metric.Count:0.000}ms max={metric.Max*1e-6:0.000}ms frames={metric.Count}; inclusive");
                    metric.Total = metric.Max = 0; metric.Count = 0;
                }
            }
            catch (Exception error) { NxBridge.Log("[engine-time] unavailable; disabled: " + error.Message); enabled = false; }
        }
        void OnDisable()
        {
            foreach (var metric in metrics) metric.Recorder.Dispose();
            metrics.Clear(); initialized = false;
        }
    }
}
