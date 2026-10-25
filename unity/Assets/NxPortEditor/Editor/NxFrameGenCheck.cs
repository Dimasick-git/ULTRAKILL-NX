using System;
using UnityEditor;
using UnityEngine;

public static class NxFrameGenCheck
{
    static void Require(bool value, string message) { if (!value) throw new Exception("[fg-pack-test] " + message); }
    public static void Check()
    {
        var shader = AssetDatabase.LoadAssetAtPath<Shader>("Assets/NxPort/Fsr/NxFrameGenCopy.shader");
        Require(shader && shader.isSupported, "packing shader unsupported");
        var material = new Material(shader);
        var input = new Texture2D(4, 1, TextureFormat.RGBAFloat, false, true);
        var output = new RenderTexture(4, 1, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
        var readback = new Texture2D(4, 1, TextureFormat.RGBA32, false, true);
        var previous = RenderTexture.active;
        bool previousSrgb = GL.sRGBWrite;
        try
        {
            Require(output.Create(), "output allocation");
            float[] depth = {0f, 0.5f, 0.9991234f, 1f};
            for (int i = 0; i < 4; i++) input.SetPixel(i, 0, new Color(depth[i], 0, 0, 0));
            input.Apply(); GL.sRGBWrite = false;
            Graphics.Blit(input, output, material, 0);
            RenderTexture.active = output; readback.ReadPixels(new Rect(0, 0, 4, 1), 0, 0); readback.Apply();
            var bytes = readback.GetRawTextureData<byte>();
            for (int i = 0; i < 4; i++)
            {
                byte[] expected = BitConverter.GetBytes(depth[i]);
                for (int c = 0; c < 4; c++) Require(bytes[i * 4 + c] == expected[c], "depth bits " + i + "/" + c);
            }
            var motion = new[] {Vector2.zero, new Vector2(-0.125f, 0.25f), new Vector2(1, -1), new Vector2(0.0078125f, -0.015625f)};
            ushort[] halves = {0, 0, 0xb000, 0x3400, 0x3c00, 0xbc00, 0x2000, 0xa400};
            for (int i = 0; i < 4; i++) input.SetPixel(i, 0, new Color(motion[i].x, motion[i].y, 0, 0));
            input.Apply(); Graphics.Blit(input, output, material, 1);
            RenderTexture.active = output; readback.ReadPixels(new Rect(0, 0, 4, 1), 0, 0); readback.Apply();
            bytes = readback.GetRawTextureData<byte>();
            for (int i = 0; i < 8; i++)
            {
                Require(bytes[i * 2] == (halves[i] & 255) && bytes[i * 2 + 1] == (halves[i] >> 8), "motion half " + i);
            }
            Debug.Log("[fg-pack-test] PASS: exact float32 depth and signed half2 motion bytes through RGBA8 GPU targets");
        }
        finally
        {
            RenderTexture.active = previous; GL.sRGBWrite = previousSrgb;
            output.Release(); UnityEngine.Object.DestroyImmediate(output); UnityEngine.Object.DestroyImmediate(input);
            UnityEngine.Object.DestroyImmediate(readback); UnityEngine.Object.DestroyImmediate(material);
        }
    }
}
