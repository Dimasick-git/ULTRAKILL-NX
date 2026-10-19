// NxGamepad.cs -- контроллер Свитча как обычный Gamepad для Input System.
//
// ULTRAKILL с 2022 года целиком поддерживает геймпад через Input System, поэтому
// эмулировать мышь или клавиатуру не нужно: заводим виртуальное устройство
// Gamepad и перед каждым обновлением Input System кладём в него состояние,
// прочитанное у загрузчика. Игра видит его как любой другой геймпад.
//
// Раскладка позиционная, как у Xbox: нижняя кнопка (B на Свитче) -- South,
// правая (A) -- East. Так «прыжок на нижней кнопке» остаётся под тем же
// пальцем, что и на других платформах.

using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;

namespace NxPort
{
    public sealed class NxGamepad : MonoBehaviour
    {
        const string LayoutName = "NxSwitchGamepad";

        // Keep positional gameplay bindings, but give UI usage bindings Nintendo
        // semantics. In the game's UI map */{Submit} and */{Cancel} resolve these
        // usages; exchanging the entire A/B state would also exchange jump/dodge.
        public static Gamepad CreatePad()
        {
            InputSystem.RegisterLayout(@"{
                ""name"": ""NxSwitchGamepad"", ""extend"": ""Gamepad"",
                ""displayName"": ""Nintendo Switch"",
                ""controls"": [
                    { ""name"": ""buttonSouth"", ""displayName"": ""B"", ""shortDisplayName"": ""B"", ""usages"": [""Cancel""] },
                    { ""name"": ""buttonEast"", ""displayName"": ""A"", ""shortDisplayName"": ""A"", ""usages"": [""Submit""] },
                    { ""name"": ""buttonWest"", ""displayName"": ""Y"", ""shortDisplayName"": ""Y"" },
                    { ""name"": ""buttonNorth"", ""displayName"": ""X"", ""shortDisplayName"": ""X"" }
                    ,{ ""name"": ""leftShoulder"", ""displayName"": ""L"", ""shortDisplayName"": ""L"" }
                    ,{ ""name"": ""rightShoulder"", ""displayName"": ""R"", ""shortDisplayName"": ""R"" }
                    ,{ ""name"": ""leftTrigger"", ""displayName"": ""ZL"", ""shortDisplayName"": ""ZL"" }
                    ,{ ""name"": ""rightTrigger"", ""displayName"": ""ZR"", ""shortDisplayName"": ""ZR"" }
                    ,{ ""name"": ""leftStick"", ""displayName"": ""LS"", ""shortDisplayName"": ""LS"" }
                    ,{ ""name"": ""rightStick"", ""displayName"": ""RS"", ""shortDisplayName"": ""RS"" }
                    ,{ ""name"": ""leftStickPress"", ""displayName"": ""LS press"", ""shortDisplayName"": ""LS press"" }
                    ,{ ""name"": ""rightStickPress"", ""displayName"": ""RS press"", ""shortDisplayName"": ""RS press"" }
                    ,{ ""name"": ""start"", ""displayName"": ""+"", ""shortDisplayName"": ""+"" }
                    ,{ ""name"": ""select"", ""displayName"": ""-"", ""shortDisplayName"": ""-"" }
                ]
            }");
            return (Gamepad)InputSystem.AddDevice(LayoutName, "Nintendo Switch (NxPort)");
        }

        Gamepad _pad;
        NxbPad _state;
        global::InputManager hints;
        float nextHints;
        static readonly System.Reflection.FieldInfo HintDevice = typeof(global::InputManager).GetField("<LastButtonDevice>k__BackingField",
            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);

        void Update()
        {
            if (Time.unscaledTime < nextHints) return;
            nextHints = Time.unscaledTime + 0.5f;
            if (!hints) hints = FindObjectOfType<global::InputManager>();
            if (!hints || HintDevice == null || hints.LastButtonDevice == _pad) return;
            HintDevice.SetValue(hints, _pad);
            foreach (var text in FindObjectsOfType<global::TextBinds>()) text.SendMessage("OnEnable", SendMessageOptions.DontRequireReceiver);
            foreach (var text in FindObjectsOfType<global::TextGetControl>()) text.SendMessage("Start", SendMessageOptions.DontRequireReceiver);
        }

        void OnEnable()
        {
            _pad = CreatePad();
            InputSystem.onBeforeUpdate += Push;
            NxBridge.Log("виртуальный Gamepad добавлен: " + _pad.deviceId + " UI: A=Submit B=Cancel");
        }

        void OnDisable()
        {
            InputSystem.onBeforeUpdate -= Push;
            if (_pad != null && _pad.added) InputSystem.RemoveDevice(_pad);
            _pad = null;
        }

        static bool Has(ulong b, NxButton m) => (b & (ulong)m) != 0;

        void Push()
        {
            if (_pad == null || !NxBridge.Pad(ref _state)) return;
            InputSystem.QueueStateEvent(_pad, MapState(_state));
        }

        public static GamepadState MapState(NxbPad state)
        {
            ulong b = state.buttons;

            var s = new GamepadState
            {
                leftStick = new Vector2(state.lx, state.ly),
                rightStick = new Vector2(state.rx, state.ry),
                leftTrigger = Has(b, NxButton.ZL) ? 1f : 0f,
                rightTrigger = Has(b, NxButton.ZR) ? 1f : 0f,
            };

            if (Has(b, NxButton.B)) s = s.WithButton(GamepadButton.South);
            if (Has(b, NxButton.A)) s = s.WithButton(GamepadButton.East);
            if (Has(b, NxButton.Y)) s = s.WithButton(GamepadButton.West);
            if (Has(b, NxButton.X)) s = s.WithButton(GamepadButton.North);
            if (Has(b, NxButton.L)) s = s.WithButton(GamepadButton.LeftShoulder);
            if (Has(b, NxButton.R)) s = s.WithButton(GamepadButton.RightShoulder);
            // Triggers live in float axes, outside the 32-bit buttons field.
            // WithButton(LeftTrigger=32/RightTrigger=33) wraps the shift to
            // DpadUp/DpadDown, causing unrelated gameplay/UI actions.
            if (Has(b, NxButton.StickL)) s = s.WithButton(GamepadButton.LeftStick);
            if (Has(b, NxButton.StickR)) s = s.WithButton(GamepadButton.RightStick);
            if (Has(b, NxButton.Plus)) s = s.WithButton(GamepadButton.Start);
            if (Has(b, NxButton.Minus)) s = s.WithButton(GamepadButton.Select);
            if (Has(b, NxButton.Up)) s = s.WithButton(GamepadButton.DpadUp);
            if (Has(b, NxButton.Down)) s = s.WithButton(GamepadButton.DpadDown);
            if (Has(b, NxButton.Left)) s = s.WithButton(GamepadButton.DpadLeft);
            if (Has(b, NxButton.Right)) s = s.WithButton(GamepadButton.DpadRight);

            return s;
        }
    }
}
