using System;
using System.Reflection;
using UnityEngine;

namespace NxPort
{
    // The game tests mainTex == null, which misses a released GPU allocation.
    // Rebuild the complete MRT/depth set together, including its command buffers.
    public static class NxGameRenderTargets
    {
        static readonly FieldInfo Reinitialize = typeof(PostProcessV2_Handler).GetField("reinitializeTextures", BindingFlags.Instance | BindingFlags.NonPublic);
        static readonly MethodInfo Setup = typeof(PostProcessV2_Handler).GetMethod("SetupRTs", BindingFlags.Instance | BindingFlags.NonPublic);

        public static bool NeedsRebuild(PostProcessV2_Handler post, int width, int height)
        {
            return !Valid(post.mainTex, width, height) || !Valid(post.depthBuffer, width, height)
                || !Valid(post.reusableBufferA, width, height) || !Valid(post.reusableBufferB, width, height)
                || !Valid(post.viewNormal, width, height);
        }

        static bool Valid(RenderTexture texture, int width, int height) =>
            texture && texture.IsCreated() && texture.width == width && texture.height == height;

        public static void Rebuild(PostProcessV2_Handler post)
        {
            if (Reinitialize == null || Setup == null) throw new MissingMethodException("PostProcessV2_Handler.SetupRTs");
            // Keep the previous native surfaces alive until the complete new
            // attachment set exists. Releasing first lets the GLES framebuffer
            // cache reuse the same surface addresses for stale attachments.
            var previousSet = new[] {post.mainTex, post.reusableBufferA, post.reusableBufferB, post.depthBuffer, post.viewNormal};
            var previousBuffers = post.buffers;
            if (post.mainCam) post.mainCam.targetTexture = null;
            if (post.hudCam) post.hudCam.targetTexture = null;
            try
            {
                post.mainTex = post.reusableBufferA = post.reusableBufferB = post.depthBuffer = post.viewNormal = null;
                post.buffers = new RenderBuffer[3];
                Reinitialize.SetValue(post, true);
                Setup.Invoke(post, null);
                var created = new[] {post.mainTex, post.reusableBufferA, post.reusableBufferB, post.depthBuffer, post.viewNormal};
                foreach (var texture in created)
                    if (!texture || (!texture.IsCreated() && !texture.Create())) throw new InvalidOperationException("Game MRT allocation failed");
                Reattach(post);
                var previous = RenderTexture.active;
                try { Graphics.SetRenderTarget(post.buffers, post.depthBuffer.depthBuffer); GL.Clear(true, true, Color.clear, 1f); }
                finally
                {
                    bool retiring = false;
                    foreach (var old in previousSet) if (ReferenceEquals(previous, old) && old) retiring = true;
                    RenderTexture.active = retiring ? null : previous;
                }
            }
            catch
            {
                Release(post.mainTex); Release(post.reusableBufferA); Release(post.reusableBufferB); Release(post.depthBuffer); Release(post.viewNormal);
                post.mainTex = previousSet[0]; post.reusableBufferA = previousSet[1]; post.reusableBufferB = previousSet[2];
                post.depthBuffer = previousSet[3]; post.viewNormal = previousSet[4]; post.buffers = previousBuffers;
                if (post.postProcessV2_VSRM) post.postProcessV2_VSRM.SetTexture("_MainTex", post.mainTex);
                if (post.mainCam && post.hudCam && post.mainTex && !NeedsRebuild(post, post.mainTex.width, post.mainTex.height)) Reattach(post);
                Reinitialize.SetValue(post, true);
                throw;
            }
            foreach (var texture in previousSet) Release(texture);
        }

        static void Release(RenderTexture texture)
        {
            if (!texture) return;
            texture.Release();
            if (Application.isPlaying) UnityEngine.Object.Destroy(texture);
            else UnityEngine.Object.DestroyImmediate(texture);
        }

        public static void Detach(PostProcessV2_Handler post)
        {
            if (!post) return;
            if (post.mainCam) post.mainCam.targetTexture = null;
            if (post.hudCam) post.hudCam.targetTexture = null;
            var current = RenderTexture.active;
            if (current && (current == post.mainTex || current == post.depthBuffer
                || current == post.reusableBufferA || current == post.reusableBufferB || current == post.viewNormal))
                RenderTexture.active = null;
        }

        public static bool SyncCameras(PostProcessV2_Handler post, Camera main)
        {
            if (!post || !main) return false;
            var hud = main.transform.Find("HUD Camera"); var output = main.transform.Find("Virtual Camera");
            if (!hud || !output) return false;
            var nextHud = hud.GetComponent<Camera>(); var nextOutput = output.GetComponent<Camera>();
            if (!nextHud || !nextOutput) return false;
            post.mainCam = main; post.hudCam = nextHud; post.virtualCam = nextOutput;
            return true;
        }

        public static void Reattach(PostProcessV2_Handler post)
        {
            // Scene unloading can reset a camera's native framebuffer binding
            // while its RenderTexture objects and cached size remain unchanged.
            if (post.buffers == null || post.buffers.Length < 3) post.buffers = new RenderBuffer[3];
            post.buffers[0] = post.mainTex.colorBuffer;
            post.buffers[1] = post.reusableBufferA.colorBuffer;
            post.buffers[2] = post.viewNormal.colorBuffer;
            post.mainCam.SetTargetBuffers(post.buffers, post.depthBuffer.depthBuffer);
            post.hudCam.SetTargetBuffers(post.mainTex.colorBuffer, post.depthBuffer.depthBuffer);
        }
    }
}
