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
        var pad = InputSystem.AddDevice<Gamepad>("NX mapping test");
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
            Debug.Log("[nxinput-test] PASS: 14 digital buttons, both trigger axes, trigger isolation, combinations and release");
        }
        finally { InputSystem.RemoveDevice(pad); }
    }

    public static void Export()
    {
        Run();
        NxBuild.AndroidExport();
    }
}
