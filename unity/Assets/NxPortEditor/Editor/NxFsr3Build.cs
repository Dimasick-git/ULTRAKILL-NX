using System;
using System.IO;
using System.Reflection;
using FidelityFX;
using FidelityFX.FSR3;
using NxPort;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

public static class NxFsr3Build
{
    const string AssetsPath = "Assets/NxPort/Fsr/NxFsr3Assets.asset";
    static void Require(bool value, string message) { if (!value) throw new Exception("[fsr3-test] " + message); }

    public static Fsr3UpscalerAssets Assets()
    {
        var assets = AssetDatabase.LoadAssetAtPath<Fsr3UpscalerAssets>(AssetsPath);
        if (!assets) { assets = ScriptableObject.CreateInstance<Fsr3UpscalerAssets>(); AssetDatabase.CreateAsset(assets, AssetsPath); }
        typeof(Fsr3UpscalerAssets).GetMethod("Reset", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(assets, null);
        EditorUtility.SetDirty(assets); AssetDatabase.SaveAssets();
        return assets;
    }

    static void TargetTest()
    {

        var main = new GameObject("World camera", typeof(Camera));
        var hud = new GameObject("HUD camera", typeof(Camera));
        var post = (PostProcessV2_Handler)System.Runtime.Serialization.FormatterServices.GetUninitializedObject(typeof(PostProcessV2_Handler));
        RenderTexture previous = RenderTexture.active;
        var pixel = new Texture2D(1,1,TextureFormat.RGBA32,false,true);
        try
        {
            post.mainCam = main.GetComponent<Camera>(); post.hudCam = hud.GetComponent<Camera>();
            post.mainCam.enabled = post.hudCam.enabled = false;
            post.mainCam.cullingMask = 0; post.mainCam.clearFlags = CameraClearFlags.SolidColor;
            post.mainTex = new RenderTexture(64,64,0); post.depthBuffer = new RenderTexture(64,64,24,RenderTextureFormat.Depth);
            post.reusableBufferA = new RenderTexture(64,64,0); post.reusableBufferB = new RenderTexture(64,64,0);
            post.viewNormal = new RenderTexture(64,64,0); post.buffers = new RenderBuffer[3];
            foreach(var rt in new[]{post.mainTex,post.depthBuffer,post.reusableBufferA,post.reusableBufferB,post.viewNormal}) Require(rt.Create(),"MRT allocation");
            Require(!NxGameRenderTargets.NeedsRebuild(post,64,64),"healthy MRT set");
            NxGameRenderTargets.Reattach(post);
            post.mainCam.backgroundColor=Color.red; post.mainCam.Render();
            post.mainCam.targetTexture=null; // Same-size camera binding lost at scene change.
            NxGameRenderTargets.Reattach(post);
            post.mainCam.backgroundColor=Color.green; post.mainCam.Render();
            RenderTexture.active=post.mainTex; pixel.ReadPixels(new Rect(32,32,1,1),0,0);pixel.Apply();
            Require(pixel.GetPixel(0,0).g>0.9f,"world rendering resumes after same-size MRT reattachment");
            post.depthBuffer.Release();
            Require(NxGameRenderTargets.NeedsRebuild(post,64,64),"released depth is detected even when managed reference remains");
            Require(NxGameRenderTargets.NeedsRebuild(post,80,64),"size mismatch detected");
            Debug.Log("[render-chain-test] PASS: lost MRT binding reattached; released depth and size mismatch detected");
        }
        finally
        {
            RenderTexture.active=previous;
            post.mainCam.targetTexture=null; post.hudCam.targetTexture=null;
            foreach(var rt in new[]{post.mainTex,post.depthBuffer,post.reusableBufferA,post.reusableBufferB,post.viewNormal}) {
                if(rt) { rt.Release(); UnityEngine.Object.DestroyImmediate(rt); }
            }
            // Avoid the game's OnDestroy touching its unset runtime singletons.
            post.mainTex=post.depthBuffer=post.reusableBufferA=post.reusableBufferB=post.viewNormal=null;
            UnityEngine.Object.DestroyImmediate(main); UnityEngine.Object.DestroyImmediate(hud);
            UnityEngine.Object.DestroyImmediate(pixel);
        }
    }

    public static void Check()
    {
        NxFrameGenCheck.Check();
        TargetTest();
        var copyMaterial = new Material(AssetDatabase.LoadAssetAtPath<Shader>("Assets/NxPort/Fsr/NxFsr3Copy.shader"));
        Require(copyMaterial.HasProperty("_MainTex") && copyMaterial.HasProperty("_World"), "Blit source and world-mask texture properties");
        var depthInput = new Texture2D(2,1,TextureFormat.RFloat,false,true);
        var depthOutput = new RenderTexture(2,1,0,RenderTextureFormat.RFloat,RenderTextureReadWrite.Linear);
        var depthReadback = new Texture2D(2,1,TextureFormat.RFloat,false,true);
        var colorInput = new Texture2D(2,1,TextureFormat.RGBAFloat,false,true);
        var linearCopy = new RenderTexture(2,1,0,RenderTextureFormat.ARGBHalf,RenderTextureReadWrite.Linear);
        var gammaCopy = new RenderTexture(2,1,0,RenderTextureFormat.ARGBHalf,RenderTextureReadWrite.Linear);
        var colorReadback = new Texture2D(2,1,TextureFormat.RGBAFloat,false,true);
        var previousDepthTarget = RenderTexture.active;
        try {
            var values = new float[]{0.9991234f,0.3001234f};
            depthInput.SetPixelData(values,0);depthInput.Apply(); Require(depthOutput.Create(),"depth-copy allocation");
            Graphics.Blit(depthInput,depthOutput,copyMaterial,2);
            RenderTexture.active=depthOutput;depthReadback.ReadPixels(new Rect(0,0,2,1),0,0);depthReadback.Apply();
            for(int i=0;i<2;i++) Require(Mathf.Abs(depthReadback.GetPixel(i,0).r-values[i])<0.000001f,"full-precision depth-copy sample "+i+" actual="+depthReadback.GetPixel(i,0).r.ToString("R")+" expected="+values[i].ToString("R"));
            Debug.Log("[fsr3-test] PASS: full-precision depth-copy shader");
            var expectedColor = new Color(0.2f,0.5f,0.8f,1);
            colorInput.SetPixels(new[]{expectedColor,expectedColor});colorInput.Apply();
            Require(linearCopy.Create() && gammaCopy.Create(),"color-copy allocations");
            Graphics.Blit(colorInput,linearCopy,copyMaterial,0);Graphics.Blit(linearCopy,gammaCopy,copyMaterial,1);
            RenderTexture.active=gammaCopy;colorReadback.ReadPixels(new Rect(0,0,2,1),0,0);colorReadback.Apply();
            Color roundtrip=colorReadback.GetPixel(0,0);
            Require(Mathf.Abs(roundtrip.r-expectedColor.r)<0.005f && Mathf.Abs(roundtrip.g-expectedColor.g)<0.005f && Mathf.Abs(roundtrip.b-expectedColor.b)<0.005f,
                "Gamma/linear copy roundtrip: "+roundtrip);
            copyMaterial.SetTexture("_World",colorInput);Graphics.Blit(colorInput,depthOutput,copyMaterial,3);
            RenderTexture.active=depthOutput;depthReadback.ReadPixels(new Rect(0,0,2,1),0,0);depthReadback.Apply();
            Require(depthReadback.GetPixel(0,0).r<0.01f,"identical composition is not reactive");
            copyMaterial.SetTexture("_World",Texture2D.blackTexture);Graphics.Blit(colorInput,depthOutput,copyMaterial,3);
            RenderTexture.active=depthOutput;depthReadback.ReadPixels(new Rect(0,0,2,1),0,0);depthReadback.Apply();
            Require(depthReadback.GetPixel(0,0).r>0.99f,"changed composition is reactive");
            Debug.Log("[fsr3-test] PASS: actual Blit source, Gamma/linear roundtrip, reactive-mask world texture binding");
        }
        finally {
            RenderTexture.active=previousDepthTarget;depthOutput.Release();linearCopy.Release();gammaCopy.Release();
            foreach(var obj in new UnityEngine.Object[]{copyMaterial,depthInput,depthOutput,depthReadback,colorInput,linearCopy,gammaCopy,colorReadback}) UnityEngine.Object.DestroyImmediate(obj);
        }
        var assets=Assets();
        var context=new Fsr3UpscalerContext();
        var color=new Texture2D(191,191,TextureFormat.RGBAFloat,false,true);
        var depth=new Texture2D(191,191,TextureFormat.RFloat,false,true);
        var motion=new Texture2D(191,191,TextureFormat.RGFloat,false,true);
        var output=new RenderTexture(256,256,0,RenderTextureFormat.ARGBHalf,RenderTextureReadWrite.Linear) {enableRandomWrite=true};
        var readback=new Texture2D(256,256,TextureFormat.RGBAFloat,false,true);
        var command=new CommandBuffer(); var previous=RenderTexture.active;
        string gpuError = null;
        Application.LogCallback captureError = (message, stack, type) => {
            if (type == LogType.Error || type == LogType.Exception || type == LogType.Assert)
                System.Threading.Interlocked.CompareExchange(ref gpuError, message.Split('\n')[0], null);
        };
        Application.logMessageReceivedThreaded += captureError;
        try
        {
            var depths=new float[191*191]; for(int i=0;i<depths.Length;i++)depths[i]=0.5f;
            depth.SetPixelData(depths,0);depth.Apply();
            motion.SetPixelData(new float[191*191*2],0);motion.Apply();
            Require(output.Create(),"output allocation");
            context.Create(new Fsr3Upscaler.ContextDescription {MaxRenderSize=new Vector2Int(191,191),MaxUpscaleSize=new Vector2Int(256,256),Shaders=assets.shaders});
            var pixels=new Color[191*191];
            for(int frame=0;frame<5;frame++)
            {
                Color expected=frame==0?new Color(0.6f,0.1f,0.05f):new Color(0.05f,0.6f,0.1f);
                for(int i=0;i<pixels.Length;i++)pixels[i]=expected;
                color.SetPixels(pixels);color.Apply(); command.Clear();
                context.Dispatch(new Fsr3Upscaler.DispatchDescription {
                    Color=new ResourceView(color),Depth=new ResourceView(depth),MotionVectors=new ResourceView(motion),Output=new ResourceView(output),
                    RenderSize=new Vector2Int(191,191),UpscaleSize=new Vector2Int(256,256),MotionVectorScale=new Vector2(-191,-191),
                    PreExposure=1,Reset=frame<2,EnableSharpening=true,Sharpness=0.5f,FrameTimeDelta=1f/60,
                    CameraNear=0.1f,CameraFar=100,CameraFovAngleVertical=60*Mathf.Deg2Rad,ViewSpaceToMetersFactor=1
                },command);
                Graphics.ExecuteCommandBuffer(command);
                RenderTexture.active=output;readback.ReadPixels(new Rect(0,0,256,256),0,0);readback.Apply();
                Require(gpuError == null, "GPU/driver error: " + gpuError);
                Color actual=readback.GetPixel(191,191);
                Require(Mathf.Abs(actual.r-expected.r)<0.08f && Mathf.Abs(actual.g-expected.g)<0.08f && Mathf.Abs(actual.b-expected.b)<0.08f,"temporal color/history reset frame "+frame+": "+actual);
                foreach(var value in readback.GetPixels()) Require(!float.IsNaN(value.r+value.g+value.b) && !float.IsInfinity(value.r+value.g+value.b),"finite temporal output");
            }
            Debug.Log("[fsr3-test] PASS: real GPU FSR 3.1 multi-pass dispatch, history reset, temporal continuation, finite pixels; "+SystemInfo.graphicsDeviceType);
        }
        finally
        {
            Application.logMessageReceivedThreaded -= captureError;
            RenderTexture.active=previous; context.Destroy(); command.Release();output.Release();
            foreach(var obj in new UnityEngine.Object[]{color,depth,motion,output,readback})UnityEngine.Object.DestroyImmediate(obj);
        }
    }

    public static void Export()
    {
        BundleOnly();
        NxTouchCheck.Export();
    }

    public static void BundleOnly()
    {
        Check(); Directory.CreateDirectory("Build/NxPortAndroid");
        var manifest=BuildPipeline.BuildAssetBundles("Build/NxPortAndroid",new[]{new AssetBundleBuild {
            assetBundleName="nx_fsr3.bundle",assetNames=new[]{AssetsPath,"Assets/NxPort/Fsr/NxFsr3Copy.shader","Assets/NxPort/Fsr/NxFrameGenCopy.shader"}
        }},BuildAssetBundleOptions.ChunkBasedCompression|BuildAssetBundleOptions.StrictMode,BuildTarget.Android);
        Require(manifest,"Android FSR3 bundle build");
        Debug.Log("[fsr3-test] Android FSR3 bundle built");
    }
}
