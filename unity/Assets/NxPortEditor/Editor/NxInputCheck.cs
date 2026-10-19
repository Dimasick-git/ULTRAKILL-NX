using System;
using NxPort;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;

// Runs against the game's actual Input System DLL in the Unity editor.
public static class NxInputCheck
{
    static void Require(bool value, string message)
    {
        if (!value) throw new Exception("[nxinput-test] " + message);
    }

    public static void Run()
    {
        var pad = NxGamepad.CreatePad();
        try
        {
            var physical = new[] { NxButton.Up, NxButton.Down, NxButton.Left, NxButton.Right,
                NxButton.X, NxButton.A, NxButton.B, NxButton.Y, NxButton.StickL,
                NxButton.StickR, NxButton.L, NxButton.R, NxButton.Plus, NxButton.Minus };
            var logical = new[] { GamepadButton.DpadUp, GamepadButton.DpadDown,
                GamepadButton.DpadLeft, GamepadButton.DpadRight, GamepadButton.North,
                GamepadButton.East, GamepadButton.South, GamepadButton.West,
                GamepadButton.LeftStick, GamepadButton.RightStick, GamepadButton.LeftShoulder,
                GamepadButton.RightShoulder, GamepadButton.Start, GamepadButton.Select };
            for (int i = 0; i < physical.Length; i++)
            {
                var state = NxGamepad.MapState(new NxbPad { buttons = (ulong)physical[i] });
                Require(state.buttons == (1u << (int)logical[i]), physical[i] + " has extra/missing buttons");
                Require(state.leftTrigger == 0 && state.rightTrigger == 0, physical[i] + " activates a trigger");
                InputState.Change(pad, state);
                Require(pad[logical[i]].isPressed, physical[i] + " control did not activate");
            }
            foreach (var buttons in new[] { NxButton.ZL, NxButton.ZR, NxButton.ZL | NxButton.ZR })
            {
                var state = NxGamepad.MapState(new NxbPad { buttons = (ulong)buttons });
                Require(state.buttons == 0, buttons + " activates digital buttons");
                InputState.Change(pad, state);
                Require(pad.leftTrigger.isPressed == ((buttons & NxButton.ZL) != 0), "left trigger mismatch");
                Require(pad.rightTrigger.isPressed == ((buttons & NxButton.ZR) != 0), "right trigger mismatch");
                Require(pad.dpad.ReadValue() == Vector2.zero && !pad.startButton.isPressed &&
                    !pad.selectButton.isPressed, "trigger activates D-pad or menu");
            }
            var combo = NxGamepad.MapState(new NxbPad { buttons = (ulong)(NxButton.ZR | NxButton.Plus | NxButton.Left) });
            InputState.Change(pad, combo);
            Require(pad.rightTrigger.isPressed && pad.startButton.isPressed && pad.dpad.left.isPressed &&
                !pad.dpad.up.isPressed && !pad.dpad.down.isPressed, "combined input mismatch");
            InputState.Change(pad, NxGamepad.MapState(default));
            Require(!pad.rightTrigger.isPressed && !pad.leftTrigger.isPressed &&
                pad.dpad.ReadValue() == Vector2.zero && !pad.startButton.isPressed, "release sticks");
            // Use the game's actual generated action asset, not a copied test
            // binding: catches both inherited usage merging and UI path changes.
            using (var actions = new global::InputActions())
            {
                var submit = actions.asset.FindAction("UI/Submit", true);
                var cancel = actions.asset.FindAction("UI/Cancel", true);
                actions.asset.devices = new InputDevice[] { pad };
                submit.Enable(); cancel.Enable();
                Require(submit.controls.Count == 1 && submit.controls[0] == pad.buttonEast,
                    "UI Submit must resolve only to physical Switch A");
                Require(cancel.controls.Count == 1 && cancel.controls[0] == pad.buttonSouth,
                    "UI Cancel must resolve only to physical Switch B");
                foreach (var button in new[] { NxButton.A, NxButton.B, NxButton.A | NxButton.B })
                {
                    InputState.Change(pad, NxGamepad.MapState(new NxbPad { buttons = (ulong)button }));
                    Require(submit.IsPressed() == ((button & NxButton.A) != 0), "A submit mismatch");
                    Require(cancel.IsPressed() == ((button & NxButton.B) != 0), "B cancel mismatch");
                }
                InputState.Change(pad, NxGamepad.MapState(default));
                Require(!submit.IsPressed() && !cancel.IsPressed(), "UI release sticks");
                Require(pad.buttonEast.displayName == "A" && pad.buttonSouth.displayName == "B",
                    "Nintendo button labels missing");
            }
            Debug.Log("[nxinput-test] PASS: 14 digital buttons, both trigger axes, trigger isolation, combinations and release");
            Debug.Log("[nxinput-test] PASS: original UI action asset, Switch A Submit / B Cancel, labels, simultaneous press and release");
            using (var hintsActions = new global::InputActions())
            {
                var manager = (global::InputManager)System.Runtime.Serialization.FormatterServices.GetUninitializedObject(typeof(global::InputManager));
                var player = (global::PlayerInput)System.Runtime.Serialization.FormatterServices.GetUninitializedObject(typeof(global::PlayerInput));
                player.Actions = hintsActions;
                const System.Reflection.BindingFlags flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
                typeof(global::InputManager).GetField("<InputSource>k__BackingField", flags).SetValue(manager, player);
                typeof(global::InputManager).GetField("<LastButtonDevice>k__BackingField", flags).SetValue(manager, pad);
                Require(manager.GetBindingString("Jump") == "B", "game tutorial Jump must show Nintendo B");
                Require(manager.GetBindingString("Move") == "LS", "game tutorial Move must show stick, not WASD composite");
                var jump = hintsActions.FindAction("Jump", true);
                int gamepadBinding = -1;
                for (int i=0;i<jump.bindings.Count;i++) if (jump.bindings[i].path.Contains("<Gamepad>")) { gamepadBinding=i;break; }
                Require(gamepadBinding >= 0, "real Jump controller binding exists");
                jump.ApplyBindingOverride(gamepadBinding, "<Gamepad>/buttonEast");
                Require(manager.GetBindingString("Jump") == "A", "tutorial hint follows actual controller remap");
                foreach (var control in new[] { pad.leftShoulder, pad.rightShoulder, pad.leftTrigger, pad.rightTrigger, pad.startButton, pad.selectButton })
                    Debug.Log("[switch-hints-test] " + control.name + "=" + control.displayName);
                Require(pad.leftShoulder.displayName=="L" && pad.rightShoulder.displayName=="R" && pad.leftTrigger.displayName=="ZL" && pad.rightTrigger.displayName=="ZR", "Nintendo shoulder/trigger labels");
                Debug.Log("[switch-hints-test] PASS: real game resolver B jump / LS move, effective user remaps, Nintendo shoulder/trigger labels");
            }
        }
        finally { InputSystem.RemoveDevice(pad); }
    }

    public static void Export()
    {
        Run();
        NxBuild.AndroidExport();
    }
}
