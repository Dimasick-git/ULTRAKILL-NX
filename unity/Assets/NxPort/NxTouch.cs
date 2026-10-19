using UnityEngine;
using UnityEngine.EventSystems;

namespace NxPort
{
    // UI input stays separate from gameplay actions: touching a setting must
    // not fire a weapon or switch the game's gamepad control scheme.
    [DefaultExecutionOrder(100)]
    public sealed class NxTouch : MonoBehaviour
    {
        NxTouchModule module;
        NxbTouch sample;
        bool focused = true;
        bool waitForRelease;

        void Update()
        {
            if (!NxBridge.Touch(ref sample)) return;
            var system = EventSystem.current;
            if (module && module.gameObject != (system ? system.gameObject : null))
            { module.Cancel(); Destroy(module); module = null; waitForRelease = true; }
            if (!system || !focused) return;
            if (waitForRelease) { if (sample.down == 0) waitForRelease = false; return; }
            if (!module) module = system.gameObject.AddComponent<NxTouchModule>();
            module.Sample(sample.down != 0, new Vector2(sample.x / 1280f * Screen.width,
                (1f - sample.y / 720f) * Screen.height));
        }

        void OnApplicationFocus(bool value)
        { focused = value; if (!value) { waitForRelease = true; if (module) module.Cancel(); } }
        void OnDisable() { if (module) { module.Cancel(); Destroy(module); } }
    }
}
