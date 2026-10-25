using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using NxPort;
using TMPro;
using UnityEditor;
using UnityEngine;

public static class NxSettingsCheck
{
    static readonly BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    static void Require(bool value, string message) { if (!value) throw new Exception("[nx-settings] " + message); }
    static void CheckRuntimeText()
    {
        var flags = BindingFlags.Static | BindingFlags.NonPublic;
        var fields = new[] { "unicodeFont", "sansFont", "cyrillicTmp", "sansTmp", "cjkTmp", "cjk" };
        var previous = new object[fields.Length];
        for (int i = 0; i < fields.Length; i++) previous[i] = typeof(NxLoc).GetField(fields[i], flags).GetValue(null);
        var vcr = AssetDatabase.LoadAssetAtPath<Font>("Assets/Font/vcr-osd-replayed.ttf");
        var sans = AssetDatabase.LoadAssetAtPath<Font>("Assets/Font/LiberationSans.ttf");
        var fallback = TMP_FontAsset.CreateFontAsset(vcr); var sansFallback = TMP_FontAsset.CreateFontAsset(sans);
        var cjkFixture = TMP_FontAsset.CreateFontAsset(vcr);
        var bitmap = new Font();
        var legacyObject = new GameObject("Legacy death label", typeof(RectTransform), typeof(UnityEngine.UI.Text));
          var deathCanvas = new GameObject("Death canvas", typeof(RectTransform), typeof(Canvas));
          deathCanvas.GetComponent<Canvas>().renderMode = RenderMode.WorldSpace;
          ((RectTransform)deathCanvas.transform).sizeDelta = new Vector2(1280, 720);
          legacyObject.transform.SetParent(deathCanvas.transform, false);
          ((RectTransform)legacyObject.transform).sizeDelta = new Vector2(1280, 680);
        var tmpObject = new GameObject("Late static TMP label", typeof(RectTransform), typeof(TextMeshProUGUI));
        var primary = TMP_FontAsset.CreateFontAsset(vcr); primary.name = "VCR_OSD_MONO_UI";
        primary.TryAddCharacters("[YOU ARE DEAD] PRESS ZR TO RESTART"); primary.atlasPopulationMode = AtlasPopulationMode.Static;
        try
        {
            object[] values = { vcr, sans, fallback, sansFallback, null, false };
            for (int i = 0; i < fields.Length; i++) typeof(NxLoc).GetField(fields[i], flags).SetValue(null, values[i]);
            NxLoc.Load(2);
            string death = "[YOU ARE DEAD]" + new string('\n', 6) + "Press RT TO RESTART";
            var legacy = legacyObject.GetComponent<UnityEngine.UI.Text>(); legacy.font = bitmap; legacy.fontStyle = FontStyle.Bold;
              legacy.resizeTextForBestFit = true; legacy.resizeTextMinSize = 10; legacy.resizeTextMaxSize = 200;
            string translated = NxLoc.TrUI(legacy, death); legacy.text = translated;
            Require(translated.StartsWith("[ВЫ МЕРТВЫ]") && translated.Contains("ZR") && translated.Split('\n').Length == 7, "death label lost translation, Switch button or spacing");
            Require(legacy.font == vcr && legacy.fontStyle == FontStyle.Bold, "legacy label lost native font family or weight");
              var deathBinding = legacyObject.GetComponent<NxDeathTextBinding>();
              Require(deathBinding, "localized death renderer missing");
              typeof(NxDeathTextBinding).GetMethod("LateUpdate", Private).Invoke(deathBinding, null);
              var deathRendered = legacyObject.transform.Find("NX Death Text").GetComponent<TextMeshProUGUI>();
              deathRendered.ForceMeshUpdate(true, true);
              Require(!legacy.enabled && deathRendered.gameObject.activeSelf, "death renderer not activated");
              Require(deathRendered.GetParsedText() == translated && deathRendered.textInfo.meshInfo[0].vertexCount > 0,
                  "localized death label has no rendered glyphs");
              Require(deathRendered.textBounds.size.x <= 1281 && deathRendered.textBounds.size.y <= 681,
                  "localized death label overflows its original rectangle");
              Debug.Log("[nx-death] PASS: Cyrillic restart ZR prompt produces visible TMP geometry inside 1280x680; vertices="
                  + deathRendered.textInfo.meshInfo[0].vertexCount);
            var late = tmpObject.GetComponent<TextMeshProUGUI>(); late.font = primary; late.fontStyle = FontStyles.Bold;
            string prompt = NxLoc.TrTmp(late, "Press Y to close"); late.text = prompt;
            Require(prompt == "Нажмите Y, чтобы закрыть", "close prompt not translated");
            Require(primary.fallbackFontAssetTable.Contains(fallback) && !primary.fallbackFontAssetTable.Contains(sansFallback), "late VCR label must retain the pixel font family");
            Require(primary.HasCharacter('В', true, true) && primary.HasCharacter('ы', true, true), "late static label cannot render Cyrillic");
            Require(late.fontStyle == FontStyles.Bold, "TMP text weight was changed");
            Require(NxLoc.Tr("PRESS \"<color=orange>Y</color>\" to CLOSE").Contains("Нажмите"), "rich text close prompt missing");
            typeof(NxLoc).GetField("cjk", flags).SetValue(null, true);
            typeof(NxLoc).GetField("cjkTmp", flags).SetValue(null, cjkFixture);
            late.font = fallback; NxLoc.TrTmp(late, "CJK font chain");
            Require(fallback.fallbackFontAssetTable.Contains(cjkFixture) && !fallback.fallbackFontAssetTable.Contains(fallback), "dynamic primary needs CJK fallback without a self cycle");
            NxLoc.Load(1); Require(legacy.font == bitmap, "English must restore original legacy font");
              typeof(NxDeathTextBinding).GetMethod("LateUpdate", Private).Invoke(deathBinding, null);
              Require(legacy.enabled && !deathRendered.gameObject.activeSelf, "English death renderer not restored");
            Debug.Log("[nx-text] PASS: legacy death text, native VCR family/weight, late TMP Cyrillic, plain/rich close prompt and English restore");
        }
        finally
        {
            for (int i = 0; i < fields.Length; i++) typeof(NxLoc).GetField(fields[i], flags).SetValue(null, previous[i]);
            UnityEngine.Object.DestroyImmediate(legacyObject); UnityEngine.Object.DestroyImmediate(tmpObject);
              UnityEngine.Object.DestroyImmediate(deathCanvas);
            UnityEngine.Object.DestroyImmediate(primary); UnityEngine.Object.DestroyImmediate(fallback);
            UnityEngine.Object.DestroyImmediate(sansFallback); UnityEngine.Object.DestroyImmediate(bitmap);
            UnityEngine.Object.DestroyImmediate(cjkFixture);
        }
    }
    static void CheckCommunity()
    {
        var parent = new GameObject("Native social panel", typeof(RectTransform), typeof(Canvas));
        parent.GetComponent<Canvas>().renderMode = RenderMode.WorldSpace;
        var root = (RectTransform)parent.transform; root.sizeDelta = new Vector2(1000, 600); root.localScale = Vector3.one * 0.75f;
        RectTransform Slot(string name, float x)
        {
            var go = new GameObject(name, typeof(RectTransform)); var rect = (RectTransform)go.transform;
            rect.SetParent(root, false); rect.anchorMin = rect.anchorMax = rect.pivot = new Vector2(0, 1);
            rect.anchoredPosition = new Vector2(x, -20); rect.sizeDelta = new Vector2(100, 28); return rect;
        }
        var keeper = Slot("Discord", 2); var twitter = Slot("Twitter", 120); var youtube = Slot("Youtube", 240);
        var text = new GameObject("Native caption", typeof(RectTransform), typeof(TextMeshProUGUI));
        text.transform.SetParent(keeper, false); var caption = text.GetComponent<TextMeshProUGUI>();
        var font = TMP_FontAsset.CreateFontAsset(AssetDatabase.LoadAssetAtPath<Font>("Assets/Font/vcr-osd-replayed.ttf"));
        try
        {
            Require(font, "native VCR font unavailable for fitting check"); caption.font = font; caption.fontSize = 24;
            typeof(NxPortSettings).GetMethod("ConfigureCommunity", BindingFlags.NonPublic | BindingFlags.Static)
                .Invoke(null, new object[] { keeper, caption, new[] { twitter, youtube } });
            Require(!twitter.gameObject.activeSelf && !youtube.gameObject.activeSelf && keeper.gameObject.activeSelf, "old social buttons remain visible");
            Require(Mathf.Abs(keeper.rect.width - 338) < 0.1f && Mathf.Abs(keeper.anchoredPosition.x - 2) < 0.1f, "Telegram must occupy the complete native row");
            caption.ForceMeshUpdate(true, true);
            Require(caption.GetParsedText() == "Telegram: @RYAZHENKACFW", "incomplete channel caption");
            var width = caption.textBounds.size.x;
            Require(caption.textInfo.lineCount == 1 && caption.textInfo.characterCount == 23, "channel caption must remain complete and on one line");
            Require(width > 0 && width <= caption.rectTransform.rect.width + 0.5f, "Telegram caption is wider than the row in the game font: " + width);
            Debug.Log("[nx-community] PASS: one 338px row, native VCR font, full single-line channel caption fits; rendered width=" + width);
        }
        finally { UnityEngine.Object.DestroyImmediate(parent); if (font) UnityEngine.Object.DestroyImmediate(font); }
    }
    public static void Check()
    {
        NxSceneMemoryCheck.CheckSequence();
        string code = "nx-settings-test-" + Guid.NewGuid().ToString("N");
        string built = Path.Combine(Application.streamingAssetsPath, "NxPort", "lang", code + ".tsv");
        string sd = Path.Combine(Application.persistentDataPath, "lang", code + ".tsv");
        Directory.CreateDirectory(Path.GetDirectoryName(built)); Directory.CreateDirectory(Path.GetDirectoryName(sd));
        try
        {
            File.WriteAllText(built, "1111111111111111\tNew builtin row\n2222222222222222\tBuiltin value\n");
            File.WriteAllText(sd, "2222222222222222\tCustom SD value\n");
            var read = typeof(NxLoc).GetMethod("Read", BindingFlags.NonPublic | BindingFlags.Static);
            var merged = (Dictionary<ulong, string>)read.Invoke(null, new object[] { code });
            Require(merged[0x1111111111111111UL] == "New builtin row" && merged[0x2222222222222222UL] == "Custom SD value", "SD merge loses bundled labels");
        }
        finally { File.Delete(built); File.Delete(sd); if (File.Exists(built + ".meta")) File.Delete(built + ".meta"); }
        // Recovered game MonoScripts cannot be attached in this editor.
        // Exercise visibility, option mapping, persistence queue and live text
        // at their public Unity boundaries; game prefab bindings are audited
        // separately against the actual shipped asset hierarchy.
        var inactive = new GameObject("Inactive", typeof(Canvas)); inactive.SetActive(false);
        var active = new GameObject("Active", typeof(Canvas));
        var owner = new GameObject("Settings test"); var port = owner.AddComponent<NxPortSettings>();
        var labelObject = new GameObject("Native TMP label", typeof(RectTransform), typeof(TextMeshProUGUI));
        var label = labelObject.GetComponent<TextMeshProUGUI>();
        var binding = labelObject.AddComponent<NxSettingsLabel>();
        try
        {
            var find = typeof(NxPortSettings).GetMethod("FirstVisible", BindingFlags.NonPublic | BindingFlags.Static);
            Require((UnityEngine.Object)find.Invoke(null, new object[] { new Behaviour[] { inactive.GetComponent<Canvas>(), active.GetComponent<Canvas>() } }) == active.GetComponent<Canvas>(), "inactive settings tree selected");
            var options = (string[])typeof(NxPortSettings).GetField("LanguageOptions", BindingFlags.Static | BindingFlags.NonPublic).GetValue(null);
            Require(options.Length == NxLocLanguages.Codes.Length + 1, "language index mapping");
            Require(options[1] == "English" && options[2] == "Russian" && options[11] == "Japanese" && options[14] == "Chinese (Traditional)", "language names do not match config IDs");
            var seen = new HashSet<string>();
            foreach (var option in options)
            {
                Require(!string.IsNullOrEmpty(option) && seen.Add(option), "blank or duplicate language name");
                foreach (char c in option) Require(c >= 32 && c < 127, "picker requires unavailable script font before selection");
            }
            var upscalers = (string[])typeof(NxPortSettings).GetField("UpscalerOptions", BindingFlags.Static | BindingFlags.NonPublic).GetValue(null);
            Require(upscalers.Length == 2 && upscalers[0] == "OFF" && upscalers[1] == "FSR 1", "experimental upscaler exposed");
            var queue = typeof(NxPortSettings).GetMethod("Queue", Private);
            queue.Invoke(port, new object[] { "gyro_sens", 3.25f }); queue.Invoke(port, new object[] { "gyro", 0f });
            var pending = (Dictionary<string, float>)typeof(NxPortSettings).GetField("pending", Private).GetValue(port);
            Require(pending["gyro_sens"] == 3.25f && pending["gyro"] == 0, "gyro SD keys"); pending.Clear();
            NxLoc.Load(1); binding.Bind(label, () => NxLoc.Tr("GYRO SENSITIVITY"));
            typeof(NxSettingsLabel).GetMethod("OnEnable", Private).Invoke(binding, null);
            NxLoc.Load(2); Require(label.text.Contains("ЧУВСТВИТЕЛЬНОСТЬ"), "native row does not translate on language change");
            Require(NxLoc.Tr("INVERT X AXIS") == "ИНВЕРТИРОВАТЬ ОСЬ X", "native uppercase label missing");
            NxLoc.Load(1); Require(label.text == "GYRO SENSITIVITY", "row does not restore English");
            CheckCommunity();
            CheckRuntimeText();
            Debug.Log("[nx-settings] PASS: visible menu, 15 readable language choices, FSR 1 only, gyro queue, native TMP live labels and SD overlay");
        }
        finally
        {
            typeof(NxSettingsLabel).GetMethod("OnDisable", Private).Invoke(binding, null);
            UnityEngine.Object.DestroyImmediate(labelObject); UnityEngine.Object.DestroyImmediate(owner);
            UnityEngine.Object.DestroyImmediate(inactive); UnityEngine.Object.DestroyImmediate(active); NxLoc.Load(1);
        }
    }
    public static void Export() { NxFontBuild.Export(); Check(); NxBuild.AndroidExport(); }
}
