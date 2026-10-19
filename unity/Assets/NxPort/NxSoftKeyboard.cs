using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using TMPro;

namespace NxPort
{
    // Android's system IME is not present on Horizon. Edit the game's real
    // fields, retaining their validation, value-changed and end-edit callbacks.
    public sealed class NxSoftKeyboard : MonoBehaviour
    {
        static RectTransform panel;
        InputField legacy;
        TMP_InputField tmp;
        string original;
        Text preview;
        Font font;
        bool caps;
        readonly System.Collections.Generic.List<Text> letters = new System.Collections.Generic.List<Text>();

        public static bool ContainsTarget(GameObject target) => panel && panel.gameObject.activeSelf && target && target.transform.IsChildOf(panel);
        string Value { get => legacy ? legacy.text : tmp ? tmp.text : ""; set { if (legacy) legacy.text = value; else if (tmp) tmp.text = value; Refresh(); } }
        int Caret { get => legacy ? legacy.caretPosition : tmp ? tmp.caretPosition : 0; set { if (legacy) legacy.caretPosition = value; else if (tmp) tmp.caretPosition = value; } }
        int Anchor => legacy ? legacy.selectionAnchorPosition : tmp ? tmp.selectionAnchorPosition : 0;

        void Update()
        {
            var selected = EventSystem.current ? EventSystem.current.currentSelectedGameObject : null;
            if (ContainsTarget(selected)) return;
            var nextLegacy = selected ? selected.GetComponent<InputField>() : null;
            var nextTmp = selected ? selected.GetComponent<TMP_InputField>() : null;
            if (nextLegacy && (!nextLegacy.IsInteractable() || nextLegacy.readOnly)) nextLegacy = null;
            if (nextTmp && (!nextTmp.IsInteractable() || nextTmp.readOnly)) nextTmp = null;
            if (nextLegacy == legacy && nextTmp == tmp) return;
            if (panel) panel.gameObject.SetActive(false);
            legacy = nextLegacy; tmp = nextTmp;
            if (!legacy && !tmp) return;
            original = Value;
            if (!panel) Build();
            panel.gameObject.SetActive(true); Refresh();
        }

        void Refresh() { if (preview) preview.text = Value; }

        public static string ReplaceRange(string text, int anchor, int focus, string insertion, out int caret)
        {
            int a = Mathf.Clamp(Mathf.Min(anchor, focus), 0, text.Length), b = Mathf.Clamp(Mathf.Max(anchor, focus), 0, text.Length);
            caret = a + insertion.Length;
            return text.Substring(0, a) + insertion + text.Substring(b);
        }
        void Insert(string text)
        {
            int position; Value = ReplaceRange(Value, Anchor, Caret, text, out position); Caret = Mathf.Min(position, Value.Length);
        }
        void Backspace()
        {
            int a = Anchor, b = Caret;
            if (a == b) a = Mathf.Max(0, b - 1);
            int position; Value = ReplaceRange(Value, a, b, "", out position); Caret = position;
        }
        void Close(bool accept)
        {
            if (!accept) Value = original;
            if (legacy) { bool focused = legacy.isFocused; legacy.DeactivateInputField(); if (!focused) legacy.onEndEdit.Invoke(legacy.text); }
            if (tmp) { bool focused = tmp.isFocused; tmp.DeactivateInputField(); if (!focused) tmp.onEndEdit.Invoke(tmp.text); }
            if (EventSystem.current) EventSystem.current.SetSelectedGameObject(null);
            legacy = null; tmp = null; panel.gameObject.SetActive(false);
        }

        RectTransform Rect(string name, Transform parent, Vector2 size, Vector2 at)
        {
            var go = new GameObject(name, typeof(RectTransform)); var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false); rect.sizeDelta = size; rect.anchoredPosition = at; return rect;
        }
        Text Label(Transform parent, string value, Vector2 size)
        {
            var text = Rect("Text", parent, size, Vector2.zero).gameObject.AddComponent<Text>();
            text.font = font; text.text = value; text.fontSize = 23; text.color = Color.white;
            text.alignment = TextAnchor.MiddleCenter; text.raycastTarget = false; return text;
        }
        Text Key(string title, Vector2 at, float width, UnityEngine.Events.UnityAction action)
        {
            var key = Rect(title, panel, new Vector2(width, 44), at);
            var image = key.gameObject.AddComponent<Image>(); image.color = new Color(.22f, .22f, .22f, 1);
            var button = key.gameObject.AddComponent<Button>(); button.targetGraphic = image;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(action); return Label(key, title, new Vector2(width - 6, 44));
        }
        void Build()
        {
            font = Resources.GetBuiltinResource<Font>("Arial.ttf");
            var root = Rect("NX keyboard", transform, Vector2.zero, Vector2.zero);
            var canvas = root.gameObject.AddComponent<Canvas>(); canvas.renderMode = RenderMode.ScreenSpaceOverlay; canvas.sortingOrder = 31000;
            var scale = root.gameObject.AddComponent<CanvasScaler>(); scale.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scale.referenceResolution = new Vector2(1280, 720); scale.matchWidthOrHeight = .5f;
            root.gameObject.AddComponent<GraphicRaycaster>();
            panel = Rect("Keyboard", root, new Vector2(1240, 332), new Vector2(0, 170));
            panel.anchorMin = panel.anchorMax = new Vector2(.5f, 0);
            panel.gameObject.AddComponent<Image>().color = new Color(.035f, .035f, .035f, .98f);
            var previewRect = Rect("Preview", panel, new Vector2(1180, 44), new Vector2(0, 138));
            preview = Label(previewRect, "", new Vector2(1160, 44)); preview.alignment = TextAnchor.MiddleLeft;
            string[] rows = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm.,-" };
            for (int row = 0; row < rows.Length; row++)
                for (int column = 0; column < rows[row].Length; column++)
                {
                    string value = rows[row][column].ToString();
                    var label = Key(value, new Vector2((column - (rows[row].Length - 1) * .5f) * 82, 84 - row * 49), 76,
                        () => Insert(caps ? value.ToUpperInvariant() : value));
                    letters.Add(label);
                }
            Key("SHIFT", new Vector2(-510, 35), 142, () => { caps = !caps; foreach (var letter in letters) letter.text = caps ? letter.text.ToUpperInvariant() : letter.text.ToLowerInvariant(); });
            Key("DELETE", new Vector2(510, 35), 142, Backspace);
            Key("<", new Vector2(-510, -14), 64, () => Caret = Mathf.Max(0, Caret - 1));
            Key(">", new Vector2(-432, -14), 64, () => Caret = Mathf.Min(Value.Length, Caret + 1));
            Key("CANCEL", new Vector2(-410, -124), 220, () => Close(false));
            Key("SPACE", new Vector2(0, -124), 380, () => Insert(" "));
            Key("APPLY", new Vector2(410, -124), 220, () => Close(true));
        }
        void OnDisable() { if (panel) panel.gameObject.SetActive(false); }
    }
}
