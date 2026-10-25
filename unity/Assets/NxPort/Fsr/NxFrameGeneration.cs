using System;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;

namespace NxPort
{
    // Unity queues capture and metadata; interpolation and presentation run on
    // the rendering thread. No second game update or physics step is issued.
    public sealed class NxFrameGeneration : IDisposable
    {
        [StructLayout(LayoutKind.Sequential)] struct Description
        {
            public uint version, color, depth, motion, generated;
            public uint renderWidth, renderHeight, outputWidth, outputHeight;
        }
        [StructLayout(LayoutKind.Sequential)] struct Frame
        {
            public ulong frameId;
            public float milliseconds, nearPlane, farPlane, fovRadians, jitterX, jitterY;
            public uint reset, reserved;
        }
        [DllImport("nxbridge")] static extern IntPtr nxb_fg_create(ref Description description);
        [DllImport("nxbridge")] static extern IntPtr nxb_fg_enqueue(IntPtr session, ref Frame frame);
        [DllImport("nxbridge")] static extern IntPtr nxb_fg_callback();
        [DllImport("nxbridge")] static extern int nxb_fg_status(IntPtr session);
        [DllImport("nxbridge")] static extern int nxb_fg_available();
        Camera world, display;
        CommandBuffer capture, present;
        AssetBundle bundle;
        Material packing;
        RenderTexture color, generated, depth, motion;
        DepthTextureMode addedDepth;
        IntPtr session, callback;
        bool reset = true;
        Vector3 lastPosition;
        Quaternion lastRotation;
        float lastFov;

        static RenderTexture Texture(int w, int h, string name)
        {
            var rt = new RenderTexture(w, h, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear)
                { name = name, filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
            if (!rt.Create()) { UnityEngine.Object.Destroy(rt); throw new InvalidOperationException(name + " allocation failed"); }
            return rt;
        }
        static uint Name(RenderTexture rt)
        {
            ulong value = unchecked((ulong)rt.GetNativeTexturePtr().ToInt64());
            if (value == 0 || value > uint.MaxValue) throw new InvalidOperationException("invalid GLES texture name");
            return (uint)value;
        }
        public NxFrameGeneration(PostProcessV2_Handler post, Camera output, int rw, int rh, int ow, int oh, Shader sharedShader = null)
        {
            try
            {
                // A process-wide fault/budget rejection must be checked before
                // creating four RTs, motion-vector passes and command buffers.
                if (nxb_fg_available() == 0)
                    throw new InvalidOperationException("FG blocked for this session; normal rendering active");
                if (!NxBridge.Available || SystemInfo.graphicsDeviceType != GraphicsDeviceType.OpenGLES3 ||
                    SystemInfo.usesReversedZBuffer || !SystemInfo.supportsMotionVectors)
                    throw new NotSupportedException("FG requires GLES3, standard depth and motion vectors");
                if (Marshal.SizeOf(typeof(Description)) != 36 || Marshal.SizeOf(typeof(Frame)) != 40)
                    throw new InvalidOperationException("FG native ABI mismatch");
                world = post.mainCam; display = output;
                Shader shader = sharedShader;
                if (!shader)
                {
                    bundle = AssetBundle.LoadFromFile(Path.Combine(Application.streamingAssetsPath, "NxPort/nx_fsr3.bundle"));
                    shader = bundle ? bundle.LoadAsset<Shader>("assets/nxport/fsr/nxframegencopy.shader") : null;
                }
                if (!shader || !shader.isSupported) throw new InvalidOperationException("FG packing shader unavailable");
                packing = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
                color = Texture(ow, oh, "NX FG real color"); generated = Texture(ow, oh, "NX FG interpolated color");
                depth = Texture(rw, rh, "NX FG packed depth"); motion = Texture(rw, rh, "NX FG packed motion");
                addedDepth = (DepthTextureMode.Depth | DepthTextureMode.MotionVectors) & ~world.depthTextureMode;
                world.depthTextureMode |= addedDepth;
                capture = new CommandBuffer { name = "NX FG world depth and motion" };
                capture.Blit(post.depthBuffer, depth, packing, 0);
                capture.Blit(BuiltinRenderTextureType.MotionVectors, motion, packing, 1);
                world.AddCommandBuffer(CameraEvent.AfterEverything, capture);
                callback = nxb_fg_callback();
                var description = new Description { version = 1, color = Name(color), depth = Name(depth), motion = Name(motion),
                    generated = Name(generated), renderWidth = (uint)rw, renderHeight = (uint)rh, outputWidth = (uint)ow, outputHeight = (uint)oh };
                session = nxb_fg_create(ref description);
                if (callback == IntPtr.Zero || session == IntPtr.Zero) throw new InvalidOperationException("FG native session unavailable");
                // The GPU marker follows the depth/motion copies. A managed
                // onPostRender stamp can arrive after a nested composition
                // callback and permanently reject otherwise valid frames.
                capture.IssuePluginEventAndData(callback, 13, session);
                present = new CommandBuffer { name = "NX AMD frame interpolation" };
                display.AddCommandBuffer(CameraEvent.AfterEverything, present);
                NxBridge.Log("[fg] capture enabled; native AMD graph initializes on render thread");
            }
            catch { Dispose(); throw; }
        }
        public void Prepare(Vector2 jitter)
        {
            if (session == IntPtr.Zero) return;
            present.Clear();
            if (nxb_fg_status(session) < 0)
            { NxBridge.Log("[fg] native backend failed; returning to normal presentation"); Dispose(); return; }
            bool cut = reset || Vector3.Distance(lastPosition, world.transform.position) > 5 ||
                Quaternion.Angle(lastRotation, world.transform.rotation) > 45 || Mathf.Abs(lastFov - world.fieldOfView) > 1;
            var frame = new Frame { frameId = (ulong)Time.frameCount, milliseconds = Mathf.Clamp(Time.unscaledDeltaTime * 1000, 1, 200),
                nearPlane = world.nearClipPlane, farPlane = world.farClipPlane, fovRadians = world.fieldOfView * Mathf.Deg2Rad,
                jitterX = jitter.x, jitterY = jitter.y, reset = cut ? 1u : 0u };
            IntPtr job = nxb_fg_enqueue(session, ref frame);
            if (job == IntPtr.Zero) { reset = true; return; }
            present.Blit(BuiltinRenderTextureType.CameraTarget, color);
            present.IssuePluginEventAndData(callback, 11, job);
            lastPosition = world.transform.position; lastRotation = world.transform.rotation; lastFov = world.fieldOfView; reset = false;
        }
        public void Dispose()
        {
            if (world && capture != null) world.RemoveCommandBuffer(CameraEvent.AfterEverything, capture);
            if (display && present != null) display.RemoveCommandBuffer(CameraEvent.AfterEverything, present);
            capture?.Release(); capture = null; present?.Release(); present = null;
            if (world) world.depthTextureMode &= ~addedDepth;
            addedDepth = 0; world = display = null;
            if (session != IntPtr.Zero && callback != IntPtr.Zero)
            {
                var release = new CommandBuffer { name = "NX FG render-thread release" };
                try { release.IssuePluginEventAndData(callback, 12, session); Graphics.ExecuteCommandBuffer(release); }
                finally { release.Release(); session = IntPtr.Zero; }
            }
            foreach (var rt in new[] {color, generated, depth, motion})
                if (rt) { rt.Release(); UnityEngine.Object.Destroy(rt); }
            color = generated = depth = motion = null;
            if (packing) UnityEngine.Object.Destroy(packing); packing = null;
            if (bundle) bundle.Unload(false); bundle = null;
        }
    }
}
