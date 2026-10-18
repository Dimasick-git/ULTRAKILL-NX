using System;
using System.IO;
using NxPort;
using UnityEditor;
using UnityEngine;

public static class NxFsrBuild
{
    const string ShaderPath = "Assets/NxPort/Fsr/NxFsr.shader";
    static void Require(bool valid, string message)
    { if (!valid) throw new Exception("[fsr-test] " + message); }

    public static void Check()
    {
        var shader = AssetDatabase.LoadAssetAtPath<Shader>(ShaderPath);
        Require(shader && shader.isSupported, "FSR shader unsupported in editor");
        var material = new Material(shader);
        var input = new Texture2D(16, 16, TextureFormat.RGBA32, false, true)
            { filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
        var pixels = new Color[256];
        for (int y=0;y<16;++y) for (int x=0;x<16;++x)
            pixels[y*16+x] = y<8 ? (x<8 ? Color.red : Color.blue) : (x<8 ? Color.green : Color.yellow);
        input.SetPixels(pixels); input.Apply();
        var easu = new RenderTexture(32,32,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear)
            { filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
        var rcas = new RenderTexture(32,32,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
        var result = new Texture2D(32,32,TextureFormat.RGBA32,false,true);
        var previous = RenderTexture.active;
        try
        {
            Require(material.passCount == 2, "expected two passes");
            Require(easu.Create() && rcas.Create(), "test RT allocation failed");
            NxFsr.SetConstants(material,16,16,32,32,0.5f);
            Graphics.Blit(input,easu,material,0);
            Graphics.Blit(easu,rcas,material,1);
            RenderTexture.active=rcas; result.ReadPixels(new Rect(0,0,32,32),0,0);result.Apply();
            foreach (var xy in new[] {new Vector2Int(3,3),new Vector2Int(28,3),new Vector2Int(3,28),new Vector2Int(28,28)})
            {
                Color expected=pixels[(xy.y/2)*16+xy.x/2], actual=result.GetPixel(xy.x,xy.y);
                Require(Mathf.Abs(expected.r-actual.r)<0.08f && Mathf.Abs(expected.g-actual.g)<0.08f &&
                    Mathf.Abs(expected.b-actual.b)<0.08f,"wrong orientation/color at "+xy+": "+actual);
            }
            foreach (var color in result.GetPixels())
                Require(!float.IsNaN(color.r+color.g+color.b) && color.r>=0 && color.g>=0 && color.b>=0,"invalid output pixel");
            string path=Path.GetFullPath("../fsr-r26-probe.png"); File.WriteAllBytes(path,result.EncodeToPNG());
            foreach (var message in ShaderUtil.GetShaderMessages(shader))
                Require(message.severity != UnityEditor.Rendering.ShaderCompilerMessageSeverity.Error, message.message);
            Debug.Log("[fsr-test] PASS: real GPU EASU+RCAS, quadrant orientation/colors, finite output; "+SystemInfo.graphicsDeviceType);
        }
        finally
        {
            RenderTexture.active=previous; easu.Release();rcas.Release();
            UnityEngine.Object.DestroyImmediate(material);UnityEngine.Object.DestroyImmediate(input);
            UnityEngine.Object.DestroyImmediate(easu);UnityEngine.Object.DestroyImmediate(rcas);UnityEngine.Object.DestroyImmediate(result);
        }
    }

    public static void Export()
    {
        Check();
        Directory.CreateDirectory("Build/NxPortAndroid");
        var bundle = new AssetBundleBuild { assetBundleName="nx_fsr.bundle", assetNames=new[] {ShaderPath} };
        var manifest=BuildPipeline.BuildAssetBundles("Build/NxPortAndroid", new[] {bundle},
            BuildAssetBundleOptions.ChunkBasedCompression|BuildAssetBundleOptions.StrictMode, BuildTarget.Android);
        Require(manifest!=null,"Android FSR bundle build failed");
        Debug.Log("[fsr-test] Android FSR bundle built");
        NxInputCheck.Export();
    }
}
