using System;
using System.Collections.Generic;
using SettingsMenu.Components;
using SettingsMenu.Models;
using TMPro;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.SceneManagement;

namespace NxPort
{
    // Native prefabs keep the game's styling, touch, focus and Back behavior.
    // There is deliberately no separate canvas or SWITCH menu.
    public sealed class NxPortSettings : MonoBehaviour
    {
        readonly Dictionary<string, float> pending = new Dictionary<string, float>();
        readonly List<ScriptableObject> definitions = new List<ScriptableObject>();
        float searchAt, saveAt;
        bool updateLinks = true;
        void OnEnable() { SceneManager.sceneLoaded += SceneLoaded; }
        void SceneLoaded(Scene scene, LoadSceneMode mode)
        {
            string name = scene.name.Replace(" ", "");
            updateLinks = name.IndexOf("MainMenu", StringComparison.OrdinalIgnoreCase) >= 0;
        }
        internal static readonly string[] LanguageOptions = {
            "System", "English", "Russian", "Ukrainian", "Deutsch", "French", "Spanish",
            "Portuguese (Brazil)", "Italiano", "Polski", "Turkish", "Japanese", "Korean",
            "Chinese (Simplified)", "Chinese (Traditional)"
        };
        internal static readonly string[] UpscalerOptions = { "OFF", "FSR 1" };

        void Update()
        {
            if (pending.Count != 0 && Time.unscaledTime >= saveAt) Flush();
            if (Time.unscaledTime < searchAt) return;
            searchAt = Time.unscaledTime + 0.5f;
            var menu = FindActiveMenu();
            if (menu) AttachNative(menu.transform);
            if (updateLinks) updateLinks = !UpdateCommunityLink();
        }

        internal static global::SettingsMenu.Components.SettingsMenu FindActiveMenu()
            => FirstVisible(FindObjectsOfType<global::SettingsMenu.Components.SettingsMenu>(true)) as global::SettingsMenu.Components.SettingsMenu;
        internal static Behaviour FirstVisible(Behaviour[] candidates)
        {
            foreach (var candidate in candidates)
                if (candidate && candidate.isActiveAndEnabled) return candidate;
            return null;
        }
        void Queue(string key, float value) { pending[key] = value; saveAt = Time.unscaledTime + 0.4f; }
        void Flush()
        {
            foreach (var setting in pending)
                if (!NxBridge.SetConfig(setting.Key, setting.Value)) NxBridge.Log("[settings] could not save " + setting.Key);
            pending.Clear();
        }
        T Definition<T>() where T : ScriptableObject
        {
            var value = ScriptableObject.CreateInstance<T>(); definitions.Add(value); return value;
        }
        static void Bind(TMP_Text text, Func<string> render)
        {
            if (!text) return;
            var binding = text.GetComponent<NxSettingsLabel>() ?? text.gameObject.AddComponent<NxSettingsLabel>();
            binding.Bind(text, render);
        }
        SettingsCategory Section(SettingsPageBuilder page, Transform content, string title)
        {
            var category = Definition<SettingsCategory>(); category.title = title;
            var header = Instantiate(page.assets.categoryTitlePrefab, content); header.name = "NX " + title;
            header.ConfigureFrom(category, page);
            Bind(header.GetComponentInChildren<TMP_Text>(true), () => "-- " + NxLoc.Tr(title.ToUpperInvariant()) + " --");
            return category;
        }
        SettingsItemBuilder Row(SettingsPageBuilder page, Transform content, string title, SettingsItemType type, out SettingsItem item)
        {
            item = Definition<SettingsItem>(); item.label = title; item.itemType = type; item.noResetButton = true;
            item.valueDisplayMultiplayer = 1; item.sideNote = "";
            item.valueType = global::SettingsMenu.Models.ValueType.Int; item.dropdownList = Array.Empty<string>();
            // Empty PreferenceKey isolates these settings from the game's prefs.
            var row = Instantiate(page.assets.itemPrefab, content); row.name = "NX " + title; return row;
        }
        void Dropdown(SettingsPageBuilder page, Transform content, SettingsCategory category,
            string title, string[] options, int value, Action<int> changed)
        {
            var row = Row(page, content, title, SettingsItemType.Dropdown, out var item);
            item.dropdownList = options; row.ConfigureFrom(item, category, page);
            var dropdown = row.GetComponentInChildren<TMP_Dropdown>(true);
            dropdown.SetValueWithoutNotify(Mathf.Clamp(value, 0, options.Length - 1)); dropdown.RefreshShownValue();
            dropdown.onValueChanged.AddListener(v => changed(v)); BindRow(row, title);
        }
        void Toggle(SettingsPageBuilder page, Transform content, SettingsCategory category, string title, string key)
        {
            var row = Row(page, content, title, SettingsItemType.Toggle, out var item);
            item.valueType = global::SettingsMenu.Models.ValueType.Bool; row.ConfigureFrom(item, category, page);
            var toggle = row.GetComponentInChildren<UnityEngine.UI.Toggle>(true);
            toggle.SetIsOnWithoutNotify(NxBridge.ConfigInt(key) != 0);
            toggle.onValueChanged.AddListener(v => Queue(key, v ? 1 : 0)); BindRow(row, title);
        }
        void Range(SettingsPageBuilder page, Transform content, SettingsCategory category,
            string title, string key, float min, float max, bool whole)
        {
            var row = Row(page, content, title, SettingsItemType.Slider, out var item);
            item.valueType = global::SettingsMenu.Models.ValueType.Float;
            item.sliderConfig = new SliderConfig { minValue = min, maxValue = max, wholeNumbers = whole,
                textConfig = new SliderValueToTextConfig { decimalType = whole ? DecimalType.NoDecimals : DecimalType.Two,
                    multiplier = 1, suffix = "", ifMin = "", ifMax = "", minColor = Color.white, maxColor = Color.white } };
            row.ConfigureFrom(item, category, page);
            var slider = row.GetComponentInChildren<Slider>(true);
            slider.SetValueWithoutNotify(Mathf.Clamp(whole ? NxBridge.ConfigInt(key) : NxBridge.ConfigFloat(key), min, max));
            slider.onValueChanged.AddListener(v => Queue(key, v)); BindRow(row, title);
        }
        static void BindRow(SettingsItemBuilder row, string title)
        {
            foreach (var text in row.GetComponentsInChildren<TMP_Text>(true))
                if (text.text == NxLoc.Tr(title.ToUpperInvariant()))
                    Bind(text, () => NxLoc.Tr(title.ToUpperInvariant()));
        }
        void AttachNative(Transform root)
        {
            AttachPage(root, "General", (page, content) => {
                var section = Section(page, content, "Language");
                Dropdown(page, content, section, "Language", LanguageOptions, NxBridge.ConfigInt("ui_language"), v => {
                    Queue("ui_language", v); NxLoc.Load(NxLoc.Resolve(v));
                });
            });
            AttachPage(root, "Controls", (page, content) => {
                var section = Section(page, content, "Gyroscope");
                Toggle(page, content, section, "Gyroscope", "gyro");
                Range(page, content, section, "Gyro sensitivity", "gyro_sens", 0.1f, 20, false);
                Toggle(page, content, section, "Invert gyro X", "gyro_invert_x");
                Toggle(page, content, section, "Invert gyro Y", "gyro_invert_y");
            });
            AttachPage(root, "Graphics", (page, content) => {
                var section = Section(page, content, "Render");
                Range(page, content, section, "Render height", "render_height", 180, 720, true);
                Dropdown(page, content, section, "Upscaler", UpscalerOptions, NxBridge.ConfigInt("fsr_enabled"), v => Queue("fsr", v));
                Range(page, content, section, "RCAS attenuation (0 = strongest)", "fsr_sharpness", 0, 2, false);
                Toggle(page, content, section, "CPU loading boost", "loading_boost");
                Toggle(page, content, section, "Enemy shader preload:", "spawn_warmup");
            });
        }
        void AttachPage(Transform root, string name, Action<SettingsPageBuilder, Transform> build)
        {
            var target = root.Find("Pages/" + name); if (!target) return;
            var page = target.GetComponent<SettingsPageBuilder>(); var content = target.Find("Scroll Rect/Contents");
            if (!page || !page.isActiveAndEnabled || !page.assets || !content || content.Find("NX " + name)) return;
            // Original BuildPage clears its container in Awake; wait for it.
            if (content.childCount == 0) return;
            build(page, content);
            var marker = new GameObject("NX " + name); marker.transform.SetParent(content, false); marker.SetActive(false);
            page.RefreshSelectableNavigation(); LayoutRebuilder.MarkLayoutForRebuild(content as RectTransform);
            NxBridge.Log("[settings] native " + name + " rows attached");
        }
        static bool UpdateCommunityLink()
        {
            bool found = false;
            foreach (var link in FindObjectsOfType<WebButton>(true))
            {
                if (!link) continue;
                if (link.gameObject.name == "NX Community" && link.url == "https://t.me/RYAZHENKACFW") { found = true; continue; }
                string url = link.url ?? "";
                if (url != "https://t.me/RYAZHENKACFW" && url != "https://discord.com/invite/newblood") continue;
                var rect = link.transform as RectTransform;
                var caption = link.GetComponentInChildren<TMP_Text>(true);
                if (!rect || !caption) continue;
                var removed = new List<RectTransform>();
                foreach (var sibling in link.transform.parent.GetComponentsInChildren<WebButton>(true))
                {
                    if (sibling.transform.parent != link.transform.parent) continue;
                    if (sibling.url == "https://youtube.com/c/Hakita" || sibling.url == "https://twitter.com/ULTRAKILLGame")
                        removed.Add(sibling.transform as RectTransform);
                }
                ConfigureCommunity(rect, caption, removed.ToArray());
                link.url = "https://t.me/RYAZHENKACFW"; link.gameObject.name = "NX Community";
                found = true;
            }
            return found;
        }

        // Reuse the entire original social row, rather than squeezing a channel
        // name into a 100px icon slot. Parent-local corners also respect scaling.
        internal static void ConfigureCommunity(RectTransform keeper, TMP_Text caption, RectTransform[] removed)
        {
            var parent = keeper.parent as RectTransform;
            if (!parent) return;
            var corners = new Vector3[4];
            Vector2 min = new Vector2(float.PositiveInfinity, float.PositiveInfinity);
            Vector2 max = new Vector2(float.NegativeInfinity, float.NegativeInfinity);
            void Include(RectTransform slot)
            {
                if (!slot || slot.parent != parent) return;
                slot.GetWorldCorners(corners);
                foreach (var corner in corners)
                {
                    Vector2 point = parent.InverseTransformPoint(corner);
                    min = Vector2.Min(min, point); max = Vector2.Max(max, point);
                }
            }
            Include(keeper); foreach (var slot in removed) Include(slot);
            foreach (var slot in removed) if (slot && slot != keeper) slot.gameObject.SetActive(false);
            keeper.anchorMin = keeper.anchorMax = keeper.pivot = new Vector2(0, 1);
            keeper.sizeDelta = max - min;
            keeper.anchoredPosition = new Vector2(min.x - parent.rect.xMin, max.y - parent.rect.yMax);
            caption.rectTransform.anchorMin = Vector2.zero; caption.rectTransform.anchorMax = Vector2.one;
            caption.rectTransform.offsetMin = new Vector2(6, 0); caption.rectTransform.offsetMax = new Vector2(-6, 0);
            caption.text = "<mark=#000000>Telegram: @RYAZHENKACFW</mark>";
            caption.richText = true; caption.enableWordWrapping = false;
            caption.enableAutoSizing = true; caption.fontSizeMin = 18; caption.fontSizeMax = 24;
            caption.overflowMode = TextOverflowModes.Overflow;
            caption.alignment = TextAlignmentOptions.Center; caption.raycastTarget = false;
        }
        void OnApplicationFocus(bool value) { if (!value && pending.Count != 0) Flush(); }
        void OnDisable() { SceneManager.sceneLoaded -= SceneLoaded; if (pending.Count != 0) Flush(); }
        void OnDestroy() { foreach (var definition in definitions) if (definition) Destroy(definition); }
    }
    public sealed class NxSettingsLabel : MonoBehaviour
    {
        TMP_Text label; Func<string> render;
        public void Bind(TMP_Text target, Func<string> value) { label = target; render = value; Refresh(); }
        void Refresh() { if (label && render != null) label.text = render(); }
        void OnEnable() { NxLoc.LanguageChanged += Refresh; Refresh(); }
        void OnDisable() { NxLoc.LanguageChanged -= Refresh; }
    }
}
