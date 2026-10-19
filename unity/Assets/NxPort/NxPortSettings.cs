using System.Collections.Generic;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.UI;

namespace NxPort
{
    public sealed class NxPortSettings : MonoBehaviour
    {
        readonly Dictionary<string, float> pending = new Dictionary<string, float>();
        Canvas canvas;
        GameObject window;
        Button launch, close;
        Text status;
        Font font;
        float searchAt, saveAt;
        bool open;
        global::SettingsMenu.Components.SettingsMenu menu;
        GameObject oldSelection;

        void Update()
        {
            if (Time.unscaledTime >= searchAt)
            {
                searchAt = Time.unscaledTime + 0.5f;
                menu = FindObjectOfType<global::SettingsMenu.Components.SettingsMenu>();
                if (menu && !canvas) Build();
            }
            if (!canvas) return;
            bool visible = menu && menu.isActiveAndEnabled;
            canvas.enabled = visible;
            if (!visible && open) SetOpen(false);
            if (pending.Count != 0 && Time.unscaledTime >= saveAt) Flush();
            if (open && Gamepad.current != null && Gamepad.current.buttonSouth.wasPressedThisFrame) SetOpen(false);
        }

        RectTransform Rect(string name, Transform parent, Vector2 size, Vector2 position)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform; rect.SetParent(parent, false);
            rect.sizeDelta = size; rect.anchoredPosition = position; return rect;
        }

        Text Label(Transform parent, string text, Vector2 size, Vector2 position, int point = 22)
        {
            var label = Rect("Label", parent, size, position).gameObject.AddComponent<Text>();
            label.font = font; label.text = text; label.fontSize = point;
            label.color = Color.white; label.alignment = TextAnchor.MiddleLeft; label.raycastTarget = false; return label;
        }

        Button Button(Transform parent, string title, Vector2 size, Vector2 position)
        {
            var rect = Rect(title, parent, size, position);
            var image = rect.gameObject.AddComponent<Image>(); image.color = new Color(0.36f, 0.035f, 0.035f, 1);
            var button = rect.gameObject.AddComponent<Button>(); button.targetGraphic = image;
            Label(rect, title, size - new Vector2(20, 0), Vector2.zero).alignment = TextAnchor.MiddleCenter;
            return button;
        }

        void Queue(string key, float value)
        { pending[key] = value; saveAt = Time.unscaledTime + 0.4f; status.text = "Saving..."; }

        void Flush()
        {
            bool okay = true;
            foreach (var setting in pending) okay &= NxBridge.SetConfig(setting.Key, setting.Value);
            pending.Clear(); if (status) status.text = okay ? "Saved to SD card" : "Could not save: check free space / SD card";
        }

        void Switch(Transform parent, string title, string key, float y)
        {
            Label(parent, title, new Vector2(560, 42), new Vector2(-85, y));
            bool value = NxBridge.ConfigInt(key == "fsr" ? "fsr_enabled" : key) != 0;
            var button = Button(parent, value ? "ON" : "OFF", new Vector2(130, 40), new Vector2(290, y));
            var label = button.GetComponentInChildren<Text>();
            button.onClick.AddListener(() => { value = !value; label.text = value ? "ON" : "OFF"; Queue(key, value ? 1 : 0); });
        }

        void Range(Transform parent, string title, string key, float min, float max, float value, float y, bool whole)
        {
            var label = Label(parent, title + "  " + value.ToString("0.##"), new Vector2(420, 42), new Vector2(-155, y));
            var rect = Rect(title, parent, new Vector2(290, 32), new Vector2(220, y));
            rect.gameObject.AddComponent<Image>().color = Color.clear;
            var track = Rect("Track", rect, new Vector2(290, 6), Vector2.zero).gameObject.AddComponent<Image>();
            track.color = new Color(0.3f, 0.3f, 0.3f); track.raycastTarget = false;
            var area = Rect("Handle area", rect, new Vector2(266, 32), Vector2.zero);
            var handle = Rect("Handle", area, new Vector2(24, 32), Vector2.zero).gameObject.AddComponent<Image>();
            handle.color = Color.cyan;
            var slider = rect.gameObject.AddComponent<Slider>(); slider.handleRect = (RectTransform)handle.transform;
            slider.targetGraphic = handle; slider.minValue = min; slider.maxValue = max; slider.wholeNumbers = whole;
            slider.value = value;
            slider.onValueChanged.AddListener(v => { label.text = title + "  " + v.ToString("0.##"); Queue(key, v); });
        }

        void Build()
        {
            font = Resources.GetBuiltinResource<Font>("Arial.ttf");
            var root = Rect("NX settings", transform, Vector2.zero, Vector2.zero);
            canvas = root.gameObject.AddComponent<Canvas>(); canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = 30000;
            var scale = root.gameObject.AddComponent<CanvasScaler>(); scale.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scale.referenceResolution = new Vector2(1280, 720); scale.matchWidthOrHeight = 0.5f;
            root.gameObject.AddComponent<GraphicRaycaster>();
            launch = Button(root, "SWITCH", new Vector2(170, 44), new Vector2(525, -320));
            launch.onClick.AddListener(() => SetOpen(true));
            window = Rect("Modal", root, new Vector2(1280, 720), Vector2.zero).gameObject;
            var blocker = window.AddComponent<Image>(); blocker.color = new Color(0, 0, 0, 0.85f);
            var panel = Rect("Panel", window.transform, new Vector2(860, 660), Vector2.zero);
            panel.gameObject.AddComponent<Image>().color = new Color(0.06f, 0.06f, 0.06f, 1);
            Label(panel, "ULTRAKILL - NX / SWITCH", new Vector2(760, 48), new Vector2(0, 286), 28);
            Range(panel, "Render height", "render_height", 180, 720, NxBridge.ConfigInt("render_height"), 216, true);
            Switch(panel, "FSR 1 / EASU + RCAS", "fsr", 158);
            Range(panel, "RCAS attenuation (0 = strongest)", "fsr_sharpness", 0, 2, NxBridge.ConfigFloat("fsr_sharpness"), 100, false);
            Switch(panel, "Gyroscope", "gyro", 42);
            Range(panel, "Gyro sensitivity", "gyro_sens", 0.1f, 20, NxBridge.ConfigFloat("gyro_sens"), -16, false);
            Switch(panel, "Invert gyro X", "gyro_invert_x", -74);
            Switch(panel, "Invert gyro Y", "gyro_invert_y", -132);
            Switch(panel, "CPU loading boost", "loading_boost", -190);
            status = Label(panel, NxBridge.ConfigInt("support_reboot") != 0 ? "SaltyNX installed: reboot Atmosphere to enable FPS overlay" : "Saved to SD card", new Vector2(760, 36), new Vector2(0, -244), 18);
            close = Button(panel, "BACK / B", new Vector2(220, 42), new Vector2(260, -292));
            close.onClick.AddListener(() => SetOpen(false));
            window.SetActive(false);
        }

        void SetOpen(bool value)
        {
            if (!value && pending.Count != 0) Flush();
            open = value; window.SetActive(value); launch.gameObject.SetActive(!value);
            var system = EventSystem.current;
            if (system)
            {
                if (value) { oldSelection = system.currentSelectedGameObject; system.SetSelectedGameObject(close.gameObject); }
                else if (oldSelection && oldSelection.activeInHierarchy) system.SetSelectedGameObject(oldSelection);
            }
        }
        void OnApplicationFocus(bool value) { if (!value && pending.Count != 0) Flush(); }
        void OnDisable() { if (pending.Count != 0) Flush(); }
    }
}
