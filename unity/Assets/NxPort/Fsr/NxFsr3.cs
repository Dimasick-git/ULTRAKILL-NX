using System;
using System.IO;
using FidelityFX;
using FidelityFX.FSR3;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;

namespace NxPort
{
    // FSR 3.1 temporal upscaling. Frame generation is not included.
    // The existing composition includes HUD pixels; mark composition changes
    // reactive so they do not inherit world-space motion/history.
    public sealed class NxFsr3 : IDisposable
    {
        AssetBundle bundle;
        Fsr3UpscalerContext context;
        Material copies;
        Camera world;
        CommandBuffer capture;
        RenderTexture linear, output, depth, motion, worldColor, reactive;
        DepthTextureMode previousDepth, addedDepth;
        Matrix4x4 projection, nonJittered;
        bool jittered, previousTransparent, reset = true;
        int worldFrame = -1;
        Vector3 lastPosition;
        Quaternion lastRotation;
        float lastFov;
        string gpuFailure;
        readonly Fsr3Upscaler.DispatchDescription dispatch = new Fsr3Upscaler.DispatchDescription();
        public Vector2 JitterOffset => dispatch.JitterOffset;
        public Shader FrameGenerationShader => bundle ? bundle.LoadAsset<Shader>("assets/nxport/fsr/nxframegencopy.shader") : null;

        public bool TargetsCreated => linear && linear.IsCreated() && output && output.IsCreated()
            && depth && depth.IsCreated() && motion && motion.IsCreated()
            && worldColor && worldColor.IsCreated() && reactive && reactive.IsCreated();

        static RenderTexture Texture(int w, int h, RenderTextureFormat format, string name, bool random = false)
        {
            var rt = new RenderTexture(w, h, 0, format, RenderTextureReadWrite.Linear)
                { name = name, enableRandomWrite = random, filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
            if (!rt.Create()) { UnityEngine.Object.Destroy(rt); throw new InvalidOperationException(name + " allocation failed"); }
            return rt;
        }

        public NxFsr3(PostProcessV2_Handler post, int iw, int ih, int ow, int oh)
        {
            try
            {
                Application.logMessageReceivedThreaded += GpuMessage;
                if (!SystemInfo.supportsComputeShaders || !SystemInfo.supportsMotionVectors)
                    throw new NotSupportedException("compute shaders/motion vectors unavailable");
                foreach (var format in new[] {GraphicsFormat.R32_SFloat, GraphicsFormat.R32_UInt,
                    GraphicsFormat.R16G16B16A16_SFloat, GraphicsFormat.R8G8B8A8_UNorm})
                    if (!SystemInfo.IsFormatSupported(format, FormatUsage.LoadStore))
                        throw new NotSupportedException("storage image format unavailable: " + format);
                bundle = AssetBundle.LoadFromFile(Path.Combine(Application.streamingAssetsPath, "NxPort/nx_fsr3.bundle"));
                var assets = bundle ? bundle.LoadAsset<Fsr3UpscalerAssets>("assets/nxport/fsr/nxfsr3assets.asset") : null;
                var shader = bundle ? bundle.LoadAsset<Shader>("assets/nxport/fsr/nxfsr3copy.shader") : null;
                if (!assets || !shader || !shader.isSupported) throw new InvalidOperationException("FSR 3.1 bundle unavailable");
                // Test all dispatch kernels before allocating temporal history.
                foreach (var cs in new[] {assets.shaders.prepareInputsPass, assets.shaders.lumaPyramidPass,
                    assets.shaders.shadingChangePyramidPass, assets.shaders.shadingChangePass,
                    assets.shaders.prepareReactivityPass, assets.shaders.lumaInstabilityPass,
                    assets.shaders.accumulatePass, assets.shaders.sharpenPass})
                    if (!cs || !cs.IsSupported(cs.FindKernel("CS"))) throw new NotSupportedException("FSR 3.1 compute kernel unsupported");
                copies = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
                linear = Texture(iw, ih, RenderTextureFormat.ARGBHalf, "NX FSR3 linear color");
                output = Texture(ow, oh, RenderTextureFormat.ARGBHalf, "NX FSR3 output", true);
                depth = Texture(iw, ih, RenderTextureFormat.RFloat, "NX FSR3 world depth");
                motion = Texture(iw, ih, RenderTextureFormat.RGHalf, "NX FSR3 world motion");
                worldColor = Texture(iw, ih, RenderTextureFormat.ARGB32, "NX FSR3 pre-HUD color");
                reactive = Texture(iw, ih, RenderTextureFormat.R8, "NX FSR3 composition mask");
                copies.SetTexture("_World", worldColor);
                context = new Fsr3UpscalerContext();
                context.Create(new Fsr3Upscaler.ContextDescription {
                    MaxUpscaleSize = new Vector2Int(ow,oh), MaxRenderSize = new Vector2Int(iw,ih), Shaders = assets.shaders,
                    Flags = SystemInfo.usesReversedZBuffer ? Fsr3Upscaler.InitializationFlags.EnableDepthInverted : 0
                });
                world = post.mainCam;
                previousDepth = world.depthTextureMode;
                addedDepth = (DepthTextureMode.Depth | DepthTextureMode.MotionVectors) & ~previousDepth;
                world.depthTextureMode |= addedDepth;
                capture = new CommandBuffer { name = "NX FSR3 capture before HUD" };
                capture.Blit(post.depthBuffer, depth, copies, 2);
                capture.Blit(BuiltinRenderTextureType.MotionVectors, motion);
                capture.Blit(post.mainTex, worldColor);
                world.AddCommandBuffer(CameraEvent.AfterEverything, capture);
                Camera.onPreCull += PreCull;
                NxBridge.Log($"[fsr3] FSR 3.1 temporal ready {iw}x{ih}->{ow}x{oh}; depth+motion+jitter+reactive; TEST");
            }
            catch { Dispose(); throw; }
        }

        void GpuMessage(string message, string stack, LogType type)
        {
            if ((type == LogType.Error || type == LogType.Exception) &&
                message.IndexOf("ffx_fsr3upscaler", StringComparison.OrdinalIgnoreCase) >= 0)
                System.Threading.Interlocked.CompareExchange(ref gpuFailure, message.Split('\n')[0], null);
        }

        void PreCull(Camera camera)
        {
            if (camera != world) return;
            if (jittered) RestoreProjection();
            projection = world.projectionMatrix; nonJittered = world.nonJitteredProjectionMatrix;
            previousTransparent = world.useJitteredProjectionMatrixForTransparentRendering;
            Fsr3Upscaler.GetJitterOffset(out float x, out float y, Time.frameCount,
                Fsr3Upscaler.GetJitterPhaseCount(linear.width, output.width));
            dispatch.JitterOffset = new Vector2(x, y);
            world.nonJitteredProjectionMatrix = projection;
            world.projectionMatrix = Matrix4x4.Translate(new Vector3(2*x/linear.width, 2*y/linear.height, 0)) * projection;
            world.useJitteredProjectionMatrixForTransparentRendering = true;
            jittered = true;
        }

        void RestoreProjection()
        {
            if (world && jittered)
            {
                world.projectionMatrix = projection; world.nonJitteredProjectionMatrix = nonJittered;
                world.useJitteredProjectionMatrixForTransparentRendering = previousTransparent;
            }
            jittered = false;
        }

        public void Rendered(Camera camera)
        {
            if (camera != world) return;
            worldFrame = Time.frameCount;
            RestoreProjection();
        }

        public void Compose(CommandBuffer commands, RenderTexture color, float attenuation)
        {
            var failure = System.Threading.Volatile.Read(ref gpuFailure);
            if (failure != null) throw new InvalidOperationException(failure);
            commands.Clear();
            if (!world || worldFrame != Time.frameCount)
            {
                reset = true;
                commands.Blit(color, BuiltinRenderTextureType.CameraTarget);
                return;
            }
            commands.Blit(color, linear, copies, 0);
            commands.Blit(color, reactive, copies, 3);
            dispatch.Color = new ResourceView(linear);
            dispatch.Depth = new ResourceView(depth);
            dispatch.MotionVectors = new ResourceView(motion);
            dispatch.Reactive = new ResourceView(reactive);
            dispatch.TransparencyAndComposition = new ResourceView(reactive);
            dispatch.Output = new ResourceView(output);
            dispatch.RenderSize = new Vector2Int(linear.width, linear.height);
            dispatch.UpscaleSize = new Vector2Int(output.width, output.height);
            dispatch.MotionVectorScale = new Vector2(-linear.width, -linear.height);
            dispatch.PreExposure = 1;
            dispatch.EnableSharpening = true;
            dispatch.Sharpness = 1 - Mathf.Clamp01(attenuation / 2);
            dispatch.FrameTimeDelta = Time.unscaledDeltaTime;
            dispatch.CameraNear = SystemInfo.usesReversedZBuffer ? world.farClipPlane : world.nearClipPlane;
            dispatch.CameraFar = SystemInfo.usesReversedZBuffer ? world.nearClipPlane : world.farClipPlane;
            dispatch.CameraFovAngleVertical = world.fieldOfView * Mathf.Deg2Rad;
            dispatch.ViewSpaceToMetersFactor = 1;
            dispatch.Reset = reset || (world.transform.position - lastPosition).sqrMagnitude > 100 ||
                Quaternion.Angle(world.transform.rotation, lastRotation) > 60 || Mathf.Abs(world.fieldOfView-lastFov) > 1;
            context.Dispatch(dispatch, commands);
            commands.Blit(output, BuiltinRenderTextureType.CameraTarget, copies, 1);
            lastPosition = world.transform.position; lastRotation = world.transform.rotation; lastFov = world.fieldOfView;
            reset = false;
        }

        static void Release(ref RenderTexture texture)
        {
            if (texture) { texture.Release(); UnityEngine.Object.Destroy(texture); }
            texture = null;
        }

        public void Dispose()
        {
            Application.logMessageReceivedThreaded -= GpuMessage;
            Camera.onPreCull -= PreCull;
            RestoreProjection();
            if (world)
            {
                if (capture != null) world.RemoveCommandBuffer(CameraEvent.AfterEverything, capture);
                world.depthTextureMode &= ~addedDepth;
            }
            capture?.Release(); capture = null;
            context?.Destroy(); context = null;
            Release(ref linear); Release(ref output); Release(ref depth); Release(ref motion); Release(ref worldColor); Release(ref reactive);
            if (copies) UnityEngine.Object.Destroy(copies); copies = null;
            if (bundle) bundle.Unload(false); bundle = null; world = null;
        }
    }
}
