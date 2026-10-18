// NX software video backend. JPEG is decoded on a native worker; only the
// current RGB frame is uploaded. Audio is streamed through Unity AudioSource.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.Video;

namespace NxPort
{
    [DefaultExecutionOrder(10000)]
    public sealed class NxVideoManager : MonoBehaviour
    {
        double nextScan;
        void OnEnable() { SceneManager.sceneLoaded += SceneLoaded; }
        void OnDisable() { SceneManager.sceneLoaded -= SceneLoaded; }
        void SceneLoaded(Scene scene, LoadSceneMode mode) { nextScan = 0; }
        void Update()
        {
            if (Time.realtimeSinceStartupAsDouble < nextScan) return;
            nextScan = Time.realtimeSinceStartupAsDouble + .5;
            foreach (var player in FindObjectsOfType<VideoPlayer>())
                if (player.enabled && player.GetComponent<NxVideoPlayer>() == null)
                    player.gameObject.AddComponent<NxVideoPlayer>();
        }
    }

    [DefaultExecutionOrder(10000)]
    public sealed class NxVideoPlayer : MonoBehaviour
    {
        [StructLayout(LayoutKind.Sequential)]
        struct VideoInfo
        {
            public uint width, height, fpsNum, fpsDen, frames, sampleRate, channels, reserved;
            public ulong audioFrames;
        }
        const string Lib = "nxbridge";
        [DllImport(Lib)] static extern int nxb_video_open(string name, IntPtr player, int playing, out VideoInfo info);
        [DllImport(Lib)] static extern void nxb_video_close(int handle);
        [DllImport(Lib)] static extern IntPtr nxb_video_frame(int handle, int wanted, out int actual);
        [DllImport(Lib)] static extern int nxb_video_audio(int handle, [Out] float[] data, int count);
        [DllImport(Lib)] static extern void nxb_video_audio_seek(int handle, int frame);
        [DllImport(Lib)] static extern double nxb_video_time(int handle);
        [DllImport(Lib)] static extern int nxb_video_status(int handle);
        [DllImport(Lib)] static extern void nxb_video_control(int handle, int action, double value);
        [DllImport(Lib)] static extern void nxb_video_texture(int handle, uint gcHandle);

        static readonly FieldInfo CachedPtr = typeof(UnityEngine.Object).GetField(
            "m_CachedPtr", BindingFlags.Instance | BindingFlags.NonPublic);
        VideoPlayer source;
        Texture2D texture;
        GCHandle textureHandle;
        VideoInfo info;
        AudioSource sound;
        AudioClip clip;
        MaterialPropertyBlock properties;
        Renderer targetRenderer;
        string key;
        int handle, lastFrame = -1, previousState = -1;
        bool prepared, ended, failed, createdSound, resumeOnEnable;
        float lastSpeed = 1;
        double retryAt;

        string SourceKey()
        {
            if (source.source == VideoSource.VideoClip)
                return source.clip ? source.clip.name : null;
            if (string.IsNullOrEmpty(source.url)) return null;
            return Path.GetFileNameWithoutExtension(Uri.UnescapeDataString(source.url));
        }

        void Awake() { source = GetComponent<VideoPlayer>(); }
        void Start() { Bind(); }

        void Bind()
        {
            if (!source || CachedPtr == null) return;
            var name = SourceKey();
            if (string.IsNullOrEmpty(name)) return;
            bool play = handle != 0 ? source.isPlaying : source.playOnAwake;
            Release();
            key = name;
            IntPtr player = (IntPtr)CachedPtr.GetValue(source);
            try { handle = nxb_video_open(name, player, play ? 1 : 0, out info); }
            catch (Exception error) { NxBridge.Log("video bridge: " + error); failed = true; return; }
            if (handle == 0)
            {
                failed = true;
                source.enabled = false;
                NxBridge.Log("video unavailable: " + name);
                Fire("errorReceived", source, "NX video file missing or invalid: " + name);
                return;
            }
            // Native Android video must never allocate a codec/surface on NX.
            source.enabled = false;
            texture = new Texture2D((int)info.width, (int)info.height, TextureFormat.RGB24, false);
            texture.wrapMode = TextureWrapMode.Clamp;
            texture.filterMode = FilterMode.Bilinear;
            textureHandle = GCHandle.Alloc(texture);
            nxb_video_texture(handle, unchecked((uint)GCHandle.ToIntPtr(textureHandle).ToInt64()));
            properties = new MaterialPropertyBlock();
            targetRenderer = source.targetMaterialRenderer;
            if (!targetRenderer && source.renderMode == VideoRenderMode.MaterialOverride)
                targetRenderer = GetComponent<Renderer>();
            if (info.channels != 0 && source.audioOutputMode != VideoAudioOutputMode.None)
            {
                if (source.audioOutputMode == VideoAudioOutputMode.AudioSource)
                    sound = source.GetTargetAudioSource(0);
                if (!sound)
                {
                    sound = gameObject.AddComponent<AudioSource>();
                    sound.playOnAwake = false;
                    sound.spatialBlend = 0;
                    createdSound = true;
                }
                int opened = handle;
                clip = AudioClip.Create("NXVideo:" + name, checked((int)info.audioFrames),
                    (int)info.channels, (int)info.sampleRate, true,
                    data => nxb_video_audio(opened, data, data.Length),
                    position => nxb_video_audio_seek(opened, position));
                sound.clip = clip;
                sound.loop = false;
            }
            prepared = ended = failed = false;
            previousState = -1; lastFrame = -1; lastSpeed = 1;
            NxBridge.Log($"video ready: {name} {info.width}x{info.height} frames={info.frames}");
        }

        void Update()
        {
            if (!source) { Destroy(this); return; }
            string current = SourceKey();
            if (current != key && Time.realtimeSinceStartupAsDouble >= retryAt)
            {
                retryAt = Time.realtimeSinceStartupAsDouble + 1;
                Bind();
            }
            if (handle == 0 || failed) return;
            int state = nxb_video_status(handle);
            if (state < 0)
            {
                failed = true;
                Fire("errorReceived", source, "NX video decode failed: " + key);
                if (sound) sound.Stop();
                return;
            }
            float speed = Mathf.Clamp(source.playbackSpeed, .05f, 4);
            if (speed != lastSpeed)
            {
                nxb_video_control(handle, 4, speed);
                lastSpeed = speed;
                if (sound) sound.pitch = speed;
            }
            double time = nxb_video_time(handle);
            double duration = (double)info.frames * info.fpsDen / info.fpsNum;
            if (time >= duration && state == 1)
            {
                Fire("loopPointReached", source);
                if (source.isLooping)
                {
                    nxb_video_control(handle, 3, 0);
                    if (sound) { sound.Stop(); nxb_video_audio_seek(handle, 0); sound.Play(); }
                    time = 0;
                }
                else
                {
                    nxb_video_control(handle, 2, 0);
                    if (sound) sound.Stop();
                    state = 2; ended = true;
                    NxBridge.Log("video ended: " + key);
                }
            }
            if (state != previousState)
            {
                if (sound)
                {
                    if (state == 0) sound.Stop();
                    else if (state == 2) sound.Pause();
                    else if (previousState == 2 && !ended) sound.UnPause();
                    else { sound.timeSamples = (int)(time*info.sampleRate); sound.Play(); }
                }
                if (state == 1) ended = false;
                previousState = state;
            }
            if (sound && state == 1 && source.audioOutputMode == VideoAudioOutputMode.Direct)
                sound.volume = source.GetDirectAudioMute(0) ? 0 : source.GetDirectAudioVolume(0);
            if (sound && state == 1 && sound.isPlaying &&
                Math.Abs(sound.timeSamples - time*info.sampleRate) > info.sampleRate*.15)
                sound.timeSamples = Mathf.Clamp((int)(time*info.sampleRate), 0, clip.samples-1);
            int wanted = Math.Min((int)info.frames-1, (int)(time*info.fpsNum/info.fpsDen));
            IntPtr rgb = nxb_video_frame(handle, wanted, out int actual);
            if (rgb != IntPtr.Zero && actual != lastFrame)
            {
                texture.LoadRawTextureData(rgb, checked((int)(info.width*info.height*3)));
                texture.Apply(false, false);
                lastFrame = actual;
                if (!prepared)
                {
                    prepared = true;
                    Fire("prepareCompleted", source);
                    NxBridge.Log("video first frame: " + key);
                }
                if (source.sendFrameReadyEvents) Fire("frameReady", source, (long)actual);
            }
            if (!prepared) return;
            if (source.renderMode == VideoRenderMode.RenderTexture && source.targetTexture)
                Graphics.Blit(texture, source.targetTexture);
            else if (source.renderMode == VideoRenderMode.MaterialOverride && targetRenderer)
            {
                targetRenderer.GetPropertyBlock(properties);
                properties.SetTexture(string.IsNullOrEmpty(source.targetMaterialProperty) ? "_MainTex" : source.targetMaterialProperty, texture);
                targetRenderer.SetPropertyBlock(properties);
            }
        }

        void OnGUI()
        {
            if (!source || !prepared || handle == 0 || nxb_video_status(handle) == 0 ||
                (source.renderMode != VideoRenderMode.CameraFarPlane && source.renderMode != VideoRenderMode.CameraNearPlane)) return;
            GUI.depth = source.renderMode == VideoRenderMode.CameraFarPlane ? 1000 : -1000;
            Color saved = GUI.color;
            GUI.color = new Color(1, 1, 1, source.targetCameraAlpha);
            Rect rect = source.targetCamera ? source.targetCamera.pixelRect : new Rect(0, 0, Screen.width, Screen.height);
            rect.y = Screen.height-rect.y-rect.height;
            GUI.DrawTexture(rect, texture, ScaleMode.ScaleToFit, false);
            GUI.color = saved;
        }

        static void Fire(string name, params object[] arguments)
        {
            var source = arguments[0] as VideoPlayer;
            var field = typeof(VideoPlayer).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic);
            try { (field?.GetValue(source) as Delegate)?.DynamicInvoke(arguments); }
            catch (Exception error) { NxBridge.Log("video callback " + name + ": " + error); }
        }
        void OnDisable()
        {
            if (handle == 0) return;
            resumeOnEnable = nxb_video_status(handle) == 1;
            nxb_video_control(handle, 2, 0);
            if (sound) sound.Pause();
        }
        void OnEnable()
        {
            if (handle != 0 && resumeOnEnable)
            {
                nxb_video_control(handle, 1, 0);
                if (sound) sound.UnPause();
            }
        }
        void Release()
        {
            if (sound) sound.Stop();
            if (handle != 0) nxb_video_close(handle);
            handle = 0;
            if (textureHandle.IsAllocated) textureHandle.Free();
            if (texture) Destroy(texture);
            if (clip) Destroy(clip);
            if (createdSound && sound) Destroy(sound);
            texture = null; sound = null; clip = null; createdSound = false;
        }
        void OnDestroy() { Release(); }
    }
}
