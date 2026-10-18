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
    public sealed class NxFsr : MonoBehaviour
    {
        AssetBundle bundle;
        Material material;
        PostProcessV2_Handler post;
        Camera virtualCamera, displayCamera;
        RenderTexture low, upscaled, oldTarget;
        CommandBuffer commands;
        float originalDownscale, nextSearch;
        int height;
        int inputFrame = -1;
        bool presented;
        string pendingFailure;
        bool failed;

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
            if (height != 360 && height != 480 && height != 540) height = 540;
            SceneManager.sceneLoaded += SceneLoaded;
            Camera.onPostRender += Rendered;
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
            }
            catch (Exception error) { Fallback(error.Message); }
        }

        void SceneLoaded(Scene scene, LoadSceneMode mode)
        {
            if (mode == LoadSceneMode.Single || !virtualCamera) { ReleaseTargets(); nextSearch = 0; }
        }

        void LateUpdate()
        {
            if (failed || !material) return;
            if (pendingFailure != null) { Fallback(pendingFailure); return; }
            if (virtualCamera && displayCamera)
            {
                if (!virtualCamera.enabled || !virtualCamera.gameObject.activeInHierarchy)
                {
                    displayCamera.enabled = false;
                    return;
                }
                // Do not silently double-scale after an options-menu change.
                if (Screen.width != upscaled.width || Screen.height != upscaled.height)
                { Fallback("display resolution changed; using normal scaling"); return; }
                if (!low.IsCreated() || !upscaled.IsCreated())
                { Fallback("render targets lost"); return; }
                virtualCamera.targetTexture = low;
                if (post) post.downscaleResolution = height;
                displayCamera.depth = virtualCamera.depth + 0.01f;
                displayCamera.enabled = true;
                return;
            }
            if (Time.unscaledTime < nextSearch) return;
            nextSearch = Time.unscaledTime + 0.5f;
            var found = FindObjectOfType<PostProcessV2_Handler>();
            if (!found || !found.virtualCam || !found.mainCam || !found.hudCam) return;
            try { Bind(found); } catch (Exception error) { Fallback(error.Message); }
        }

        void Bind(PostProcessV2_Handler found)
        {
            ReleaseTargets();
            int ow = Screen.width, oh = Screen.height;
            if (ow < 1280 || oh < 720 || height >= oh)
                throw new InvalidOperationException("native 720p output required before enabling FSR");
            if (QualitySettings.activeColorSpace != ColorSpace.Gamma)
                throw new InvalidOperationException("this integration requires the game's Gamma color space");
            int width = Mathf.RoundToInt((float)ow * height / oh);
            post = found; virtualCamera = found.virtualCam;
            originalDownscale = post.downscaleResolution; oldTarget = virtualCamera.targetTexture;
            // SetupRTs uses this field for all world, depth, normals and HUD RTs.
            post.downscaleResolution = height;
            low = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Default)
                { name = "NX FSR input", filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
            upscaled = new RenderTexture(ow, oh, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Default)
                { name = "NX FSR EASU", filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
            if (!low.Create() || !upscaled.Create()) throw new InvalidOperationException("FSR RT allocation failed");
            SetConstants(material, width, height, ow, oh, NxBridge.ConfigFloat("fsr_sharpness"));
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
            commands.BeginSample("NX FSR1 EASU");
            commands.Blit(low, upscaled, material, 0);
            commands.EndSample("NX FSR1 EASU");
            commands.BeginSample("NX FSR1 RCAS");
            commands.Blit(upscaled, BuiltinRenderTextureType.CameraTarget, material, 1);
            commands.EndSample("NX FSR1 RCAS");
            displayCamera.AddCommandBuffer(CameraEvent.AfterEverything, commands);
            NxBridge.Log($"[fsr] active EASU+RCAS {width}x{height}->{ow}x{oh} format=ARGB32 HUD=in-input");
        }

        void Rendered(Camera camera)
        {
            if (camera == virtualCamera) inputFrame = Time.frameCount;
            if (camera != displayCamera || !displayCamera) return;
            if (inputFrame != Time.frameCount)
            { pendingFailure = "composition camera did not render before FSR display"; return; }
            if (!presented)
            {
                presented = true;
                NxBridge.Log($"[fsr] first output frame={inputFrame} input={low.width}x{low.height} output={upscaled.width}x{upscaled.height}");
            }
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
            if (displayCamera)
            {
                displayCamera.enabled = false;
                if (commands != null) displayCamera.RemoveCommandBuffer(CameraEvent.AfterEverything, commands);
                Destroy(displayCamera.gameObject);
            }
            commands?.Release(); commands = null;
            if (virtualCamera) virtualCamera.targetTexture = oldTarget;
            if (post) post.downscaleResolution = originalDownscale;
            if (low) { low.Release(); Destroy(low); }
            if (upscaled) { upscaled.Release(); Destroy(upscaled); }
            displayCamera = null; virtualCamera = null; post = null; low = upscaled = oldTarget = null;
            inputFrame = -1; presented = false;
        }

        void OnDisable()
        {
            SceneManager.sceneLoaded -= SceneLoaded;
            Camera.onPostRender -= Rendered;
            ReleaseTargets();
            if (material) Destroy(material);
            if (bundle) bundle.Unload(false);
            material = null; bundle = null;
        }
    }
}
