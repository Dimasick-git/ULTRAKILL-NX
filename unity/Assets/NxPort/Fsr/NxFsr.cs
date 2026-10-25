using System;
using System.IO;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace NxPort
{
    // The game's virtual camera already composites its world/HUD RT. Keep that
    // final composition at low resolution and upscale it exactly once on a
    // separate empty display camera. The first implementation includes the HUD;
    // moving it after the upscale needs a separate depth/composition change.
    [DefaultExecutionOrder(1000)]
    public sealed class NxFsr : MonoBehaviour
    {
        AssetBundle bundle;
        Material material;
        PostProcessV2_Handler post;
        Camera virtualCamera, displayCamera;
        Camera worldCamera, hudCamera;
        RenderTexture low, upscaled, oldTarget;
        CommandBuffer commands;
        float originalDownscale, nextSearch;
        int height;
        int inputFrame = -1;
        bool presented;
        string pendingFailure;
        bool failed;
        float nextSettings, sharpness;
        bool filter;
        float nextHealth;
        bool sceneRebind;
        bool temporalRequested;
        NxFsr3 temporal;
        bool temporalBlocked;
        RenderTexture gameColor, gameDepth;
        NxFrameGeneration frameGeneration;
        bool frameGenerationRequested;
        readonly NxRenderTransition transition = new NxRenderTransition();
        int worldFrame = -1;
        bool nativeComposition;
        float nextFaultCheck;
        float faultDelay = 0.5f;

        public static void SetConstants(Material target, int iw, int ih, int ow, int oh, float sharpness)
        {
            float sx = (float)iw / ow, sy = (float)ih / oh;
            target.SetVector("_Con0", new Vector4(sx, sy, 0.5f*sx-0.5f, 0.5f*sy-0.5f));
            target.SetVector("_Con1", new Vector4(1f/iw, 1f/ih, 1f/iw, -1f/ih));
            target.SetVector("_Con2", new Vector4(-1f/iw, 2f/ih, 1f/iw, 2f/ih));
            target.SetVector("_Con3", new Vector4(0, 4f/ih, 0, 0));
            target.SetVector("_RcasCon", new Vector4(Mathf.Pow(2, -Mathf.Clamp(sharpness, 0, 2)), 0, 0, 0));
            target.SetVector("_OutputSize", new Vector4(ow, oh, 1f/ow, 1f/oh));
        }

        void OnEnable()
        {
            height = NxBridge.ConfigInt("render_height");
            height = Mathf.Clamp(height, 180, 720);
            sharpness = NxBridge.ConfigFloat("fsr_sharpness");
            filter = NxBridge.ConfigInt("fsr_enabled") != 0;
            temporalRequested = false; // Stable renderer: FSR 1 only, including with an older loader.
            frameGenerationRequested = false;
            SceneManager.sceneLoaded += SceneLoaded;
            SceneManager.sceneUnloaded += SceneUnloaded;
            Camera.onPostRender += Rendered;
        }

        bool EnsureMaterial()
        {
            if (material) return true;
            try
            {
                // StreamingAssets on the extracted Android layout is assets/.
                string path = Path.Combine(Application.streamingAssetsPath, "NxPort/nx_fsr.bundle");
                bundle = AssetBundle.LoadFromFile(path);
                Shader shader = bundle ? bundle.LoadAsset<Shader>("assets/nxport/fsr/nxfsr.shader") : null;
                if (!shader || !shader.isSupported) throw new InvalidOperationException("shader/bundle unavailable: " + path);
                material = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
                if (material.passCount != 2) throw new InvalidOperationException("FSR needs EASU and RCAS");
                NxBridge.Log("[fsr] shader loaded, passes=2; waiting for game composition camera");
                return true;
            }
            catch (Exception error) { Fallback(error.Message); return false; }
        }

        void SceneLoaded(Scene scene, LoadSceneMode mode)
        {
            faultDelay = 0.5f;
            // Gameplay scenes load additively. Their persistent composition
            // handler can survive while its world camera/bindings are replaced.
            sceneRebind = true;
            nextSearch = 0;
            transition.SceneChanged();
        }

        void SceneUnloaded(Scene scene) { sceneRebind = true; nextSearch = 0; transition.SceneChanged(); }

        void Recover(string reason)
        {
            pendingFailure = null;
            ReleaseTargets();
            nextSearch = Time.unscaledTime + 0.25f;
            NxBridge.Log("[fsr] rebuilding render chain: " + reason);
        }

        void LateUpdate()
        {
            if (SceneHelper.PendingScene != null)
            {
                if (post || displayCamera || nativeComposition) ReleaseTargets();
                return;
            }
            if (failed) return;
            if (Time.unscaledTime >= nextFaultCheck)
            {
                nextFaultCheck = Time.unscaledTime + 0.5f;
                uint faults = NxBridge.RenderFaults();
                if (faults != 0)
                {
                    nextFaultCheck = Time.unscaledTime + faultDelay;
                    faultDelay = Mathf.Min(faultDelay * 2, 4);
                    Recover("GPU framebuffer/allocation error 0x" + faults.ToString("x"));
                    NxBridge.MemoryTrim(true);
                    return;
                }
                faultDelay = 0.5f;
            }
            if (pendingFailure != null) { Recover(pendingFailure); return; }
            if (sceneRebind) { sceneRebind = false; ReleaseTargets(); nextSearch = 0; }
            if (transition.TakeReset(Time.frameCount) && post && post.mainCam && post.hudCam)
            {
                NxGameRenderTargets.Reattach(post);
                NxBridge.Log("[fsr] scene settled: MRT bindings refreshed without reallocating textures");
            }
            if (Time.unscaledTime >= nextSettings)
            {
                nextSettings = Time.unscaledTime + 0.25f;
                int requested = Mathf.Clamp(NxBridge.ConfigInt("render_height"), 180, 720);
                float requestedSharpness = NxBridge.ConfigFloat("fsr_sharpness");
                bool requestedFilter = NxBridge.ConfigInt("fsr_enabled") != 0;
                bool requestedTemporal = false;
                bool requestedGeneration = false;
                if (requested != height || requestedSharpness != sharpness || requestedFilter != filter || requestedTemporal != temporalRequested || requestedGeneration != frameGenerationRequested)
                {
                    height = requested; sharpness = requestedSharpness; filter = requestedFilter;
                    if (temporalRequested != requestedTemporal) temporalBlocked = false;
                    temporalRequested = requestedTemporal;
                    frameGenerationRequested = requestedGeneration;
                    ReleaseTargets(); nextSearch = 0;
                }
            }
            if (!filter && !frameGenerationRequested)
            {
                NativeComposition();
                return;
            }
            // Plain game rendering does not require an FSR bundle, material
            // or shader compilation. Load them only when enabling the filter.
            if (!EnsureMaterial()) return;
            if (virtualCamera && displayCamera)
            {
                if (!post || !post.isActiveAndEnabled || post.virtualCam != virtualCamera ||
                    post.mainCam != worldCamera || post.hudCam != hudCamera ||
                    post.mainTex != gameColor || post.depthBuffer != gameDepth)
                { Recover("game cameras changed"); return; }
                if (!virtualCamera.enabled || !virtualCamera.gameObject.activeInHierarchy)
                {
                    displayCamera.enabled = false;
                    return;
                }
                // Do not silently double-scale after an options-menu change.
                if (Screen.width != upscaled.width || Screen.height != upscaled.height)
                { Recover("display resolution changed"); return; }
                if (!low.IsCreated() || !upscaled.IsCreated())
                { Recover("upscaler render targets lost"); return; }
                if (presented && Time.unscaledTime >= nextHealth)
                {
                    nextHealth = Time.unscaledTime + 0.5f;
                    if (NxGameRenderTargets.NeedsRebuild(post, low.width, low.height) ||
                        (temporal != null && !temporal.TargetsCreated))
                    { Recover("game MRT/depth targets lost or resized"); return; }
                }
                virtualCamera.targetTexture = low;
                if (post) post.downscaleResolution = height;
                displayCamera.depth = virtualCamera.depth + 0.01f;
                displayCamera.enabled = true;
                return;
            }
            if (Time.unscaledTime < nextSearch) return;
            nextSearch = Time.unscaledTime + 0.5f;
            var found = FindReady();
            if (!found) return;
            try { Bind(found); } catch (Exception error) { Recover(error.Message); nextSearch = Time.unscaledTime + 1; }
        }

        void NativeComposition()
        {
            // OFF must use the game's original composition camera directly.
            // No extra low/output RTs, display camera or per-frame FSR blits.
            if (nativeComposition && post && virtualCamera &&
                post.mainCam == worldCamera && post.hudCam == hudCamera && post.virtualCam == virtualCamera &&
                post.mainTex == gameColor && post.depthBuffer == gameDepth)
            {
                post.downscaleResolution = height;
                if (Time.unscaledTime >= nextHealth)
                {
                    nextHealth = Time.unscaledTime + 0.5f;
                    if (NxGameRenderTargets.NeedsRebuild(post, (int)((float)Screen.width * height / Screen.height), height))
                        Recover("native composition MRT/depth lost");
                }
                return;
            }
            if (post) { Recover("native composition cameras changed"); return; }
            if (Time.unscaledTime < nextSearch) return;
            nextSearch = Time.unscaledTime + 0.5f;
            var found = FindReady();
            if (!found) return;
            try
            {
                post = found; originalDownscale = post.downscaleResolution;
                post.downscaleResolution = height;
                NxGameRenderTargets.Rebuild(post);
                worldCamera = post.mainCam; hudCamera = post.hudCam; virtualCamera = post.virtualCam;
                oldTarget = virtualCamera.targetTexture;
                gameColor = post.mainTex; gameDepth = post.depthBuffer;
                nativeComposition = true; nextHealth = Time.unscaledTime + 0.5f;
                NxBridge.Log($"[fsr] OFF: original game compositor {gameColor.width}x{gameColor.height}->{Screen.width}x{Screen.height}; no extra camera/RT/blit chain");
            }
            catch (Exception error) { Recover("native composition setup: " + error.Message); nextSearch = Time.unscaledTime + 1; }
        }

        static PostProcessV2_Handler FindReady()
        {
            var found = FindObjectOfType<PostProcessV2_Handler>();
            if (!found || !found.isActiveAndEnabled) return null;
            var controller = FindObjectOfType<CameraController>();
            var main = controller && controller.cam ? controller.cam : found.mainCam;
            return NxGameRenderTargets.SyncCameras(found, main) ? found : null;
        }

        void Bind(PostProcessV2_Handler found)
        {
            ReleaseTargets();
            int ow = Screen.width, oh = Screen.height;
            if (ow < 1280 || oh < 720 || height > oh)
                throw new InvalidOperationException("native 720p output required before enabling FSR");
            if (QualitySettings.activeColorSpace != ColorSpace.Gamma)
                throw new InvalidOperationException("this integration requires the game's Gamma color space");
            // Match the game's integer truncation in SetupRTs for arbitrary
            // heights selected in the settings panel (not just 540/720).
            int width = (int)((float)ow * height / oh);
            post = found; virtualCamera = found.virtualCam;
            originalDownscale = post.downscaleResolution; oldTarget = virtualCamera.targetTexture;
            // SetupRTs uses this field for all world, depth, normals and HUD RTs.
            post.downscaleResolution = height;
            NxGameRenderTargets.Rebuild(post);
            // SetupRTs can discover/reassign camera references itself.
            virtualCamera = post.virtualCam;
            worldCamera = post.mainCam; hudCamera = post.hudCam;
            gameColor = post.mainTex; gameDepth = post.depthBuffer;
            nextHealth = Time.unscaledTime + 0.5f;
            low = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Default)
                { name = "NX FSR input", filterMode = filter ? FilterMode.Point : FilterMode.Bilinear, wrapMode = TextureWrapMode.Clamp };
            upscaled = new RenderTexture(ow, oh, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Default)
                { name = "NX FSR EASU", filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
            if (!low.Create() || !upscaled.Create()) throw new InvalidOperationException("FSR RT allocation failed");
            SetConstants(material, width, height, ow, oh, sharpness);
            virtualCamera.targetTexture = low;
            var output = new GameObject("NX FSR display") { hideFlags = HideFlags.HideAndDontSave };
            output.transform.SetParent(transform, false);
            displayCamera = output.AddComponent<Camera>();
            displayCamera.depth = virtualCamera.depth + 0.01f;
            displayCamera.cullingMask = 0;
            displayCamera.clearFlags = CameraClearFlags.Nothing;
            displayCamera.allowHDR = false; displayCamera.allowMSAA = false;
            displayCamera.useOcclusionCulling = false;
            commands = new CommandBuffer { name = "NX FSR1 EASU + RCAS" };
            if (filter && temporalRequested && !temporalBlocked)
            {
                try { temporal = new NxFsr3(post, width, height, ow, oh); }
                catch (Exception error) { temporalBlocked = true; NxBridge.Log("[fsr3] unavailable; using FSR 1: " + error.Message); }
            }
            if (temporal != null) commands.Blit(low, BuiltinRenderTextureType.CameraTarget);
            else if (filter) {
            commands.BeginSample("NX FSR1 EASU");
            commands.Blit(low, upscaled, material, 0);
            commands.EndSample("NX FSR1 EASU");
            commands.BeginSample("NX FSR1 RCAS");
            commands.Blit(upscaled, BuiltinRenderTextureType.CameraTarget, material, 1);
            commands.EndSample("NX FSR1 RCAS");
            } else commands.Blit(low, BuiltinRenderTextureType.CameraTarget);
            displayCamera.AddCommandBuffer(CameraEvent.AfterEverything, commands);
            if (frameGenerationRequested)
            {
                try { frameGeneration = new NxFrameGeneration(post, displayCamera, width, height, ow, oh, temporal?.FrameGenerationShader); }
                catch (Exception error) { NxBridge.Log("[fg] unavailable: " + error.Message); }
            }
            string algorithm = temporal != null ? "FSR 3.1 temporal" : filter ? "FSR 1 EASU+RCAS" : "bilinear";
            NxBridge.Log($"[fsr] active {algorithm} {width}x{height}->{ow}x{oh} HUD=in-input");
        }

        void Rendered(Camera camera)
        {
            if (camera == worldCamera) worldFrame = Time.frameCount;
            if (nativeComposition)
            {
                if (camera == virtualCamera && post && gameColor)
                {
                    if (!presented)
                    {
                        presented = true;
                        NxBridge.Log($"[fsr] native output frame={Time.frameCount} world_rendered={worldFrame == Time.frameCount}");
                    }
                    NxBridge.FsrApplied(gameColor.width, gameColor.height, Screen.width, Screen.height, false);
                    transition.Presented(Time.frameCount, worldFrame == Time.frameCount);
                }
                return;
            }
            temporal?.Rendered(camera);
            if (camera == virtualCamera)
            {
                inputFrame = Time.frameCount;
                if (temporal != null)
                {
                    try { temporal.Compose(commands, low, sharpness); }
                    catch (Exception error) { temporalBlocked = true; pendingFailure = "FSR 3.1 dispatch failed: " + error.Message; }
                }
                try { frameGeneration?.Prepare(temporal != null ? temporal.JitterOffset : Vector2.zero); }
                catch (Exception error) { frameGeneration?.Dispose(); frameGeneration = null; NxBridge.Log("[fg] disabled: " + error.Message); }
            }
            if (camera != displayCamera || !displayCamera) return;
            if (inputFrame != Time.frameCount)
            { pendingFailure = "composition camera did not render before FSR display"; return; }
            if (!presented)
            {
                presented = true;
                NxBridge.Log($"[fsr] first output frame={inputFrame} input={low.width}x{low.height} output={upscaled.width}x{upscaled.height}");
            }
            NxBridge.FsrApplied(low.width, low.height, upscaled.width, upscaled.height, filter);
            transition.Presented(Time.frameCount, worldFrame == Time.frameCount);
        }

        void Fallback(string reason)
        {
            failed = true;
            ReleaseTargets();
            Screen.SetResolution(height*16/9, height, true);
            NxBridge.Log("[fsr] fallback to normal scaling: " + reason);
        }

        void ReleaseTargets()
        {
            frameGeneration?.Dispose(); frameGeneration = null;
            temporal?.Dispose(); temporal = null;
            if (displayCamera)
            {
                displayCamera.enabled = false;
                if (commands != null) displayCamera.RemoveCommandBuffer(CameraEvent.AfterEverything, commands);
                Destroy(displayCamera.gameObject);
            }
            commands?.Release(); commands = null;
            if (virtualCamera && virtualCamera.targetTexture == low) virtualCamera.targetTexture = oldTarget;
            if (post) post.downscaleResolution = originalDownscale;
            if (low) { low.Release(); Destroy(low); }
            if (upscaled) { upscaled.Release(); Destroy(upscaled); }
            displayCamera = null; virtualCamera = null; post = null; low = upscaled = oldTarget = null;
            worldCamera = hudCamera = null;
            gameColor = gameDepth = null;
            inputFrame = -1; presented = false;
            worldFrame = -1;
            nativeComposition = false;
        }

        void OnDisable()
        {
            SceneManager.sceneLoaded -= SceneLoaded;
            SceneManager.sceneUnloaded -= SceneUnloaded;
            Camera.onPostRender -= Rendered;
            ReleaseTargets();
            if (material) Destroy(material);
            if (bundle) bundle.Unload(false);
            material = null; bundle = null;
        }
    }
}
