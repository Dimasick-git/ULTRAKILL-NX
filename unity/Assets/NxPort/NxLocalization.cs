// NxLocalization.cs -- перевод текстов игры на 14 языков.
//
// У ULTRAKILL нет своей локализации, поэтому перевод подставляется там, где
// текст попадает на экран. tools/patch_nx_localization.py вставляет вызовы
// NxLoc.TrTmp / NxLoc.TrUI в сеттеры TMP_Text.text и UI.Text.text, в
// TMP_Text.SetText(шаблон, ...) и в разбор статичного текста сцен
// (TMP_Text.ParseInputText, Text.OnEnable).
//
// Таблица языка -- lang/<код>.tsv: «хеш английской строки<TAB>перевод», хеш --
// FNV-1a 64 по символам UTF-16. Сначала ищется вся строка, затем куски между
// тегами разметки и переводами строк: так переводятся и строки, которые код
// игры собирает из частей («PRESS "<color=orange>» + кнопка + ...). Таблицы
// лежат в StreamingAssets/NxPort/lang, файл на SD (<папка игры>/lang)
// перекрывает встроенный. Английский -- без таблицы.
//
// Шрифты: VCR и LiberationSans из игры уже содержат кириллицу и латиницу
// других языков. Для японского, корейского и китайского нужен шрифт CJK из
// бандла StreamingAssets/NxPort/nx_fonts.bundle: TMP получает его запасным
// шрифтом, у UI.Text шрифт меняется, когда в переводе есть иероглифы.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using TMPro;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace NxPort
{
    public static class NxLocLanguages
    {
        // номер в настройке ui_language = индекс + 1; 0 -- язык системы
        public static readonly string[] Codes =
            { "en", "ru", "uk", "de", "fr", "es", "pt-BR", "it", "pl", "tr", "ja", "ko", "zh-Hans", "zh-Hant" };
        public static readonly string[] Names =
            { "English", "Русский", "Українська", "Deutsch", "Français", "Español", "Português (Brasil)",
              "Italiano", "Polski", "Türkçe", "日本語", "한국어", "简体中文", "繁體中文" };
        public const int FirstCjk = 11;
    }

    public static class NxLoc
    {
        // без инициализаторов полей: Tr зовётся из кода TMP и UI до любой загрузки
        static Dictionary<ulong, string> map;       // хеш английской строки -> перевод
        static Dictionary<string, string> cache;    // составные строки, переведённые по кускам
        static Dictionary<string, string> reverse;  // перевод -> английский, для смены языка на лету
        static int language;
        static bool cjk;
        static Font cjkFont;
        static Font unicodeFont;
        static Font sansFont;
        static AssetBundle fontBundle;
        static TMP_FontAsset cjkTmp, cyrillicTmp, sansTmp;

        public static int Language => language;
        public static event Action LanguageChanged;

        public static int Resolve(int setting)
        {
            if (setting >= 1 && setting <= NxLocLanguages.Codes.Length) return setting;
            int system = NxBridge.ConfigInt("system_language");
            return system >= 1 && system <= NxLocLanguages.Codes.Length ? system : 1;
        }

        static ulong Hash(string s, int start, int count)
        {
            ulong h = 14695981039346656037UL;
            for (int i = start, end = start + count; i < end; i++) { h ^= s[i]; h *= 1099511628211UL; }
            return h;
        }

        public static string Tr(string s)
        {
            if (s != null && s.StartsWith("[YOU ARE DEAD]", StringComparison.Ordinal))
                s = s.Replace("Press RT TO RESTART", "Press ZR TO RESTART")
                    .Replace("Press R2 TO RESTART", "Press ZR TO RESTART")
                    .Replace("Press [R] TO RESTART", "Press ZR TO RESTART");
            var m = map;
            if (m == null || string.IsNullOrEmpty(s)) return s;
            if (m.TryGetValue(Hash(s, 0, s.Length), out var t)) { Remember(t, s); return t; }
            if (cache.TryGetValue(s, out t)) return t;
            t = Segments(s, m) ?? s;
            if (cache.Count > 4096) cache.Clear();
            cache[s] = t;
            if (!ReferenceEquals(t, s)) Remember(t, s);
            return t;
        }

        static void Remember(string translated, string english)
        {
            if (reverse.Count > 8192) reverse.Clear();
            reverse[translated] = english;
        }

        // куски между тегами <...> и переводами строк; пробелы по краям куска сохраняются
        static string Segments(string s, Dictionary<ulong, string> m)
        {
            bool letters = false;
            for (int k = 0; k < s.Length && !letters; k++) letters = char.IsLetter(s[k]);
            if (!letters) return null;
            StringBuilder sb = null;
            int i = 0, n = s.Length, last = 0;
            while (i < n)
            {
                int start = i;
                while (i < n && s[i] != '<' && s[i] != '\n' && s[i] != '\r') i++;
                int a = start, b = i;
                while (a < b && char.IsWhiteSpace(s[a])) a++;
                while (b > a && char.IsWhiteSpace(s[b - 1])) b--;
                if (b > a && m.TryGetValue(Hash(s, a, b - a), out var t))
                {
                    if (sb == null) sb = new StringBuilder(n + 16);
                    sb.Append(s, last, a - last).Append(t);
                    last = b;
                }
                if (i < n)
                {
                    if (s[i] == '<') { int close = s.IndexOf('>', i); i = close < 0 ? n : close + 1; }
                    else i++;
                }
            }
            if (sb == null) return null;
            return sb.Append(s, last, n - last).ToString();
        }

        static bool HasCjk(string s)
        {
            foreach (char c in s)
                if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xFF00 && c <= 0xFFEF))
                    return true;
            return false;
        }

        // вызывается из Text.text / Text.OnEnable (patch_nx_localization.py)
        public static string TrUI(Text text, string s)
        {
            var r = Tr(s);
            if (text)
            {
                var binding = text.GetComponent<NxLegacyFontBinding>();
                var original = binding ? binding.Original : text.font;
                Font target = original;
                if (!SupportsUnicode(original, r))
                {
                    var fallback = cjk && HasCjk(r) ? cjkFont :
                        original && original.name.StartsWith("LiberationSans") && sansFont ? sansFont : unicodeFont;
                    if (fallback && SupportsUnicode(fallback, r)) target = fallback;
                }
                if (target && target != text.font)
                {
                    if (!binding) { binding = text.gameObject.AddComponent<NxLegacyFontBinding>(); binding.Original = original; }
                    text.font = target;
                }
                // The legacy death label's font generator can remain blank on
                // Switch even though HasCharacter succeeds. Render localized
                // death text through the TMP path already used by the HUD.
                var death = text.GetComponent<NxDeathTextBinding>();
                if (!death && language > 1 && s != null && s.StartsWith("[YOU ARE DEAD]", StringComparison.Ordinal) && text.gameObject.scene.IsValid())
                    death = text.gameObject.AddComponent<NxDeathTextBinding>();
                if (death) death.Show(text, r, language > 1, cjk && cjkTmp ? cjkTmp : cyrillicTmp);
            }
            return r;
        }

        static bool SupportsUnicode(Font font, string value)
        {
            if (!font) return false;
            if (value != null) foreach (char c in value)
                if (c > 127 && !char.IsWhiteSpace(c) && !char.IsControl(c) && !font.HasCharacter(c)) return false;
            return true;
        }

        // вызывается из TMP_Text.text / SetText / ParseInputText; шрифты -- запасными
        public static string TrTmp(TMP_Text text, string s)
        {
            if (text && text.font) AttachFallbacks(text.font);
            return Tr(s);
        }

        static Dictionary<ulong, string> Read(string code)
        {
            string sd = Path.Combine(Application.persistentDataPath, "lang", code + ".tsv");
            string built = Path.Combine(Application.streamingAssetsPath, "NxPort", "lang", code + ".tsv");
            bool hasSd = File.Exists(sd), hasBuilt = File.Exists(built);
            if (!hasSd && !hasBuilt) { NxBridge.Log("[lang] нет таблицы " + code); return null; }
            var table = new Dictionary<ulong, string>(4096);
            var sb = new StringBuilder();
            // Keep new builtin rows when the SD has an older, partial table.
            // Custom SD rows still win for keys they explicitly contain.
            foreach (var path in new[] { built, sd })
            {
            if (!File.Exists(path)) continue;
            foreach (var line in File.ReadAllLines(path, Encoding.UTF8))
            {
                int tab = line.IndexOf('\t');
                if (tab != 16 || line.Length == 0 || line[0] == '#') continue;
                if (!ulong.TryParse(line.Substring(0, 16), System.Globalization.NumberStyles.HexNumber, null, out var h)) continue;
                sb.Length = 0;
                for (int i = tab + 1; i < line.Length; i++)
                {
                    char c = line[i];
                    if (c == '\\' && i + 1 < line.Length)
                    {
                        char e = line[++i];
                        sb.Append(e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e);
                    }
                    else sb.Append(c);
                }
                table[h] = sb.ToString();
            }
            }
            NxBridge.Log($"[lang] {code}: строк {table.Count} ({(hasSd ? "сборка + SD" : "сборка")})");
            return table;
        }

        public static void Load(int lang)
        {
            if (lang < 1 || lang > NxLocLanguages.Codes.Length) lang = 1;
            var old = reverse;
            reverse = new Dictionary<string, string>();
            cache = new Dictionary<string, string>();
            map = lang == 1 ? null : Read(NxLocLanguages.Codes[lang - 1]);
            language = lang;
            cjk = lang >= NxLocLanguages.FirstCjk;
            if (cjk) LoadCjkFont();
            EnsureFallbacks();
            ApplyAll(old);
            LanguageChanged?.Invoke();
        }

        static void LoadCjkFont()
        {
            if (Application.isEditor) return; // Android bundles are verified by the font export, not loaded by the editor.
            if (!fontBundle)
            {
                string path = Path.Combine(Application.streamingAssetsPath, "NxPort", "nx_fonts.bundle");
                if (File.Exists(path)) fontBundle = AssetBundle.LoadFromFile(path);
            }
            if (!fontBundle) return;
            // Preserve the game's pixel face instead of substituting a sans
            // face of a different weight when a static atlas lacks Cyrillic.
            if (!unicodeFont) unicodeFont = fontBundle.LoadAsset<Font>("assets/font/vcr-osd-replayed.ttf");
            if (!sansFont) sansFont = fontBundle.LoadAsset<Font>("assets/font/liberationsans.ttf");
            if (!cyrillicTmp && unicodeFont) cyrillicTmp = TMP_FontAsset.CreateFontAsset(unicodeFont);
            if (!sansTmp && sansFont) sansTmp = TMP_FontAsset.CreateFontAsset(sansFont);
            if (cjk || !unicodeFont)
            {
                if (!cjkFont) cjkFont = fontBundle.LoadAsset<Font>("NxCjk");
                if (!cjkTmp && cjkFont) cjkTmp = TMP_FontAsset.CreateFontAsset(cjkFont);
            }
        }

        // запасные шрифты TMP: кириллица и прочая латиница -- из LiberationSans игры, CJK -- свой
        public static void EnsureFallbacks()
        {
            if (language > 1) LoadCjkFont();
            if (!unicodeFont)
                foreach (var asset in Resources.FindObjectsOfTypeAll<Font>())
                    if (asset.dynamic && asset.name.StartsWith("vcr-osd") && asset.HasCharacter('Я'))
                    { unicodeFont = asset; break; }
            if (!cyrillicTmp)
                foreach (var asset in Resources.FindObjectsOfTypeAll<TMP_FontAsset>())
                    if (asset.atlasPopulationMode == AtlasPopulationMode.Dynamic && asset.name.StartsWith("vcr-osd")) { cyrillicTmp = asset; break; }
            if (!cyrillicTmp && unicodeFont) cyrillicTmp = TMP_FontAsset.CreateFontAsset(unicodeFont);
            // Some scenes contain only static bitmap fonts. The bundled Noto
            // font includes Cyrillic as well as CJK and is a reliable fallback.
            if (language > 1 && (!unicodeFont || !cyrillicTmp))
            {
                LoadCjkFont();
                if (!unicodeFont) unicodeFont = cjkFont;
                if (!cyrillicTmp) cyrillicTmp = cjkTmp;
            }
            foreach (var asset in Resources.FindObjectsOfTypeAll<TMP_FontAsset>())
                AttachFallbacks(asset);
        }

        static void AttachFallbacks(TMP_FontAsset asset)
        {
            if (!asset || asset == cjkTmp) return;
            if (asset.fallbackFontAssetTable == null) asset.fallbackFontAssetTable = new List<TMP_FontAsset>();
            var list = asset.fallbackFontAssetTable;
            var preferred = asset.name.StartsWith("LiberationSans") && sansTmp ? sansTmp : cyrillicTmp;
            if (preferred && preferred != asset && !list.Contains(preferred)) list.Insert(0, preferred);
            if (cjk && cjkTmp && !list.Contains(cjkTmp)) list.Add(cjkTmp);
        }

        // текст, уже стоящий в компонентах: вернуть к английскому через прежнюю таблицу и перевести заново
        public static void Refresh() { EnsureFallbacks(); ApplyAll(reverse); }

        static void ApplyAll(Dictionary<string, string> old)
        {
            foreach (var text in Resources.FindObjectsOfTypeAll<Text>())
            {
                string cur = text.text;
                if (string.IsNullOrEmpty(cur)) continue;
                string en = old != null && old.TryGetValue(cur, out var e) ? e : cur;
                string next = TrUI(text, en);
                if (next != cur) text.text = next;
            }
            foreach (var text in Resources.FindObjectsOfTypeAll<TMP_Text>())
            {
                string cur = text.text;
                if (string.IsNullOrEmpty(cur)) continue;
                string en = old != null && old.TryGetValue(cur, out var e) ? e : cur;
                string next = TrTmp(text, en);
                if (next != cur) text.text = next;
            }
        }
    }

    public sealed class NxLegacyFontBinding : MonoBehaviour
    {
        [SerializeField] internal Font Original;
    }

    public sealed class NxDeathTextBinding : MonoBehaviour
    {
        Text source;
        TextMeshProUGUI rendered;
        bool localized;
        public void Show(Text original, string value, bool translated, TMP_FontAsset font)
        {
            source = original;
            localized = translated && font;
            if (!rendered && localized)
            {
                var go = new GameObject("NX Death Text", typeof(RectTransform), typeof(TextMeshProUGUI));
                go.layer = original.gameObject.layer;
                var rect = (RectTransform)go.transform; rect.SetParent(original.transform, false);
                rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one; rect.offsetMin = rect.offsetMax = Vector2.zero;
                rendered = go.GetComponent<TextMeshProUGUI>();
                rendered.alignment = TextAlignmentOptions.Center;
                rendered.richText = true; rendered.enableWordWrapping = true;
                rendered.overflowMode = TextOverflowModes.Overflow;
                NxBridge.Log("[lang] localized death label uses native VCR TMP renderer");
            }
            if (!rendered) return;
            rendered.gameObject.SetActive(localized);
            if (!localized) return;
            rendered.font = font; rendered.text = value;
            rendered.fontSize = original.fontSize;
            rendered.enableAutoSizing = original.resizeTextForBestFit;
            rendered.fontSizeMin = original.resizeTextMinSize;
            rendered.fontSizeMax = original.resizeTextMaxSize;
            rendered.fontStyle = original.fontStyle == FontStyle.Bold ? FontStyles.Bold :
                original.fontStyle == FontStyle.Italic ? FontStyles.Italic :
                original.fontStyle == FontStyle.BoldAndItalic ? FontStyles.Bold | FontStyles.Italic : FontStyles.Normal;
            rendered.raycastTarget = original.raycastTarget;
            rendered.color = original.color;
        }
        void LateUpdate()
        {
            if (!source) return;
            // Defer component activation until after Text.text's setter has
            // completed, avoiding recursive OnEnable while changing language.
            if (source.enabled == localized) source.enabled = !localized;
            if (localized && rendered) rendered.color = source.color;
        }
    }

    public sealed class NxLocalization : MonoBehaviour
    {
        void Start()
        {
            NxLoc.Load(NxLoc.Resolve(NxBridge.ConfigInt("ui_language")));
            SceneManager.sceneLoaded += Loaded;
        }
        void Loaded(Scene scene, LoadSceneMode mode) => NxLoc.Refresh();
        void OnDestroy() { SceneManager.sceneLoaded -= Loaded; }
    }
}
