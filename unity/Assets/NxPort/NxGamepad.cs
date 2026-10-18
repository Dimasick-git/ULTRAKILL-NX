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
        Gamepad _pad;
        NxbPad _state;

        void OnEnable()
        {
            _pad = InputSystem.AddDevice<Gamepad>("Nintendo Switch (NxPort)");
            InputSystem.onBeforeUpdate += Push;
            NxBridge.Log("виртуальный Gamepad добавлен: " + _pad.deviceId);
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
