using System;
using NxPort;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;



public static class NxTouchCheck
{
    static void Require(bool value, string what) { if (!value) throw new Exception("[nxtouch-test] " + what); }
    static GameObject Element(string name, Transform parent, Vector2 size)
    {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false); ((RectTransform)go.transform).sizeDelta = size;
        return go;
    }

    public static void Run()
    {
        int caret;
        Require(NxSoftKeyboard.ReplaceRange("12345", 4, 1, ".", out caret) == "1.5" && caret == 2, "keyboard replaces reversed selection");
        Require(NxSoftKeyboard.ReplaceRange("123", -10, 50, "7", out caret) == "7" && caret == 1, "keyboard clamps selection range");
        var events = new GameObject("Touch test events", typeof(EventSystem));
        var root = new GameObject("Touch test canvas", typeof(RectTransform), typeof(Canvas), typeof(GraphicRaycaster));
        var renderCamera = new GameObject("Touch test camera", typeof(Camera));
        try
        {
            var module = events.AddComponent<NxTouchModule>();
            typeof(BaseInputModule).GetMethod("OnEnable", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(module, null);
            renderCamera.transform.position = new Vector3(0, 0, -10);
            root.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceCamera;
            root.GetComponent<Canvas>().worldCamera = renderCamera.GetComponent<Camera>();
            RaycasterManager.GetRaycasters().Add(root.GetComponent<GraphicRaycaster>());
            var buttonGo = Element("Button", root.transform, new Vector2(400, 180));
            buttonGo.AddComponent<Image>(); var button = buttonGo.AddComponent<Button>();
            int clicks = 0; button.onClick.AddListener(() => clicks++);
            Canvas.ForceUpdateCanvases(); renderCamera.GetComponent<Camera>().Render();
            renderCamera.GetComponent<Camera>().Render();
            var center = new Vector2(Screen.width / 2f, Screen.height / 2f);
            module.Sample(true, center); module.Sample(false, center);
            Require(clicks == 1, "single tap emits exactly one click");
            module.Sample(false, center); Require(clicks == 1, "idle release does not repeat click");
            module.Sample(true, center); module.Sample(false, Vector2.zero);
            Require(clicks == 1, "release outside does not click");
            module.Sample(true, center); module.Cancel(); module.Sample(false, center);
            Require(clicks == 1, "focus cancellation does not click");
            button.interactable = false;
            module.Sample(true, center); module.Sample(false, center);
            Require(clicks == 1, "disabled buttons keep original rules");
            UnityEngine.Object.DestroyImmediate(buttonGo);

            var sliderGo = Element("Slider", root.transform, new Vector2(400, 80));
            sliderGo.AddComponent<Image>(); var slider = sliderGo.AddComponent<Slider>();
            var handle = Element("Handle", sliderGo.transform, new Vector2(24, 80)); handle.AddComponent<Image>();
            slider.handleRect = (RectTransform)handle.transform; slider.value = 0.2f;
            Canvas.ForceUpdateCanvases(); renderCamera.GetComponent<Camera>().Render();
            module.Sample(true, center - new Vector2(150, 0));
            module.Sample(true, center + new Vector2(150, 0)); module.Sample(false, center + new Vector2(150, 0));
            Require(slider.value > 0.8f, "slider drag reaches far end");
            UnityEngine.Object.DestroyImmediate(sliderGo);

            var scrollGo = Element("Scroll", root.transform, new Vector2(400, 220)); scrollGo.AddComponent<Image>();
            var scroll = scrollGo.AddComponent<ScrollRect>(); scroll.horizontal = false; scroll.inertia = false;
            var content = Element("Content", scrollGo.transform, new Vector2(400, 900));
            scroll.content = (RectTransform)content.transform; scroll.viewport = (RectTransform)scrollGo.transform;
            Canvas.ForceUpdateCanvases(); renderCamera.GetComponent<Camera>().Render(); var previous = scroll.content.anchoredPosition;
            module.Sample(true, center); module.Sample(true, center + new Vector2(0, 20));
            module.Sample(true, center + new Vector2(0, 100));
            module.Sample(false, center + new Vector2(0, 100));
            Require((scroll.content.anchoredPosition - previous).sqrMagnitude > 100, "scroll list follows finger drag");
            var cameraGo = new GameObject("RT camera", typeof(Camera));
            var texture = new RenderTexture(640, 360, 0);
            try
            {
                cameraGo.GetComponent<Camera>().targetTexture = texture;
                root.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceCamera;
                root.GetComponent<Canvas>().worldCamera = cameraGo.GetComponent<Camera>();
                var mapped = NxTouchModule.MapPoint(new Vector2(Screen.width, Screen.height), root.GetComponent<GraphicRaycaster>());
                Require(mapped == new Vector2(640, 360), "FSR render texture coordinates reach far edge");
            }
            finally { UnityEngine.Object.DestroyImmediate(cameraGo); UnityEngine.Object.DestroyImmediate(texture); }
            // Original game MonoBehaviours cannot be instantiated in this recovered
            // editor project. Exercise the real private type's event ABI without
            // activating game singletons; world terminal routing needs a device run.
            var terminalType = typeof(ShopButton).Assembly.GetType("ControllerPointer", true);
            var terminal = System.Runtime.Serialization.FormatterServices.GetUninitializedObject(terminalType);
            var pressed = new UnityEngine.Events.UnityEvent(); int terminalClicks = 0;
            pressed.AddListener(() => terminalClicks++);
            terminalType.GetField("onPressed", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).SetValue(terminal, pressed);
            Require(ReferenceEquals(NxTouchModule.TerminalPressEvent(terminal), pressed), "real terminal event property bridge");
            NxTouchModule.TerminalPressEvent(terminal).Invoke();
            Require(terminalClicks == 1 && NxTouchModule.TerminalPressEvent(null) == null && NxTouchModule.TerminalPressEvent(new object()) == null,
                "terminal event invoked once; missing/wrong target ignored");
            Require(!module.IsModuleSupported() && !module.ShouldActivateModule(), "original gamepad navigation module stays active");
            Debug.Log("[nxtouch-test] PASS: tap, release outside, cancellation, disabled button, slider drag, scroll drag, FSR coordinate mapping, terminal event ABI, navigation isolation");
        }
        finally { RaycasterManager.GetRaycasters().Remove(root.GetComponent<GraphicRaycaster>()); UnityEngine.Object.DestroyImmediate(root); UnityEngine.Object.DestroyImmediate(events); UnityEngine.Object.DestroyImmediate(renderCamera); }
    }
    public static void Export() { Run(); NxWeaponSlotsCheck.Export(); }
}
