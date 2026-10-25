using System;
using System.IO;
using System.Reflection;
using NxPort;
using UnityEditor;
using UnityEngine;

// Supply a static, OFL-licensed TTF at Assets/NxPortFonts/NxCjk.ttf.
// Its Unity asset name must match NxLoc.LoadCjkFont's lookup.
public static class NxFontBuild
{
    public static void Export()
    {
        const string path = "Assets/NxPortFonts/NxCjk.ttf";
        AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport);
        var font = AssetDatabase.LoadAssetAtPath<Font>(path);
        if (!font || font.name != "NxCjk") throw new Exception("Missing NxCjk static font");
        foreach (char c in "日本語한국어简体中文繁體中文")
            if (!font.HasCharacter(c)) throw new Exception("NxCjk missing glyph: " + c);
        // Regression: a native language label is not a translation-table hit.
        // It still needs CJK glyphs when Tr returns the original string object.
        var flags = BindingFlags.NonPublic | BindingFlags.Static;
        var cjk = typeof(NxLoc).GetField("cjk", flags);
        var cjkFont = typeof(NxLoc).GetField("cjkFont", flags);
        var previousCjk = cjk.GetValue(null);
        var previousFont = cjkFont.GetValue(null);
        var obj = new GameObject("NxCjkLabelCheck", typeof(UnityEngine.UI.Text));
        try
        {
            cjk.SetValue(null, true);
            cjkFont.SetValue(null, font);
            var text = obj.GetComponent<UnityEngine.UI.Text>();
            var label = NxLoc.TrUI(text, "日本語");
            if (label != "日本語" || text.font != font)
                throw new Exception("CJK language literal did not select its font");
        }
        finally
        {
            cjk.SetValue(null, previousCjk);
            cjkFont.SetValue(null, previousFont);
            UnityEngine.Object.DestroyImmediate(obj);
        }
        const string output = "Build/NxPortFontsAndroid";
        Directory.CreateDirectory(output);
        foreach (var uiFont in new[] { "Assets/Font/vcr-osd-replayed.ttf", "Assets/Font/LiberationSans.ttf" })
        {
            var face = AssetDatabase.LoadAssetAtPath<Font>(uiFont);
            if (!face) throw new Exception("Missing game UI font: " + uiFont);
            foreach (char c in "ВЫ МЕРТВЫ Нажмите чтобы закрыть")
                if (!char.IsWhiteSpace(c) && !face.HasCharacter(c)) throw new Exception("UI font lacks Cyrillic: " + uiFont + " " + c);
        }
        var build = new AssetBundleBuild { assetBundleName = "nx_fonts.bundle", assetNames = new[] { path,
            "Assets/Font/vcr-osd-replayed.ttf", "Assets/Font/LiberationSans.ttf" } };
        var manifest = BuildPipeline.BuildAssetBundles(output, new[] { build },
            BuildAssetBundleOptions.ChunkBasedCompression | BuildAssetBundleOptions.StrictMode, BuildTarget.Android);
        if (!manifest) throw new Exception("Android font bundle build failed");
        Debug.Log("[nx-font] PASS: CJK glyphs, Cyrillic in native VCR/Sans, native language label; Android bundle built");
    }
}
