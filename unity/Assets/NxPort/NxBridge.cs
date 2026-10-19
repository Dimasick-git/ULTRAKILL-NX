// NxBridge.cs -- P/Invoke к загрузчику ultrakill_nx (loader/source/nx_bridge.c).
//
// IL2CPP на Android делает dlopen("libnxbridge.so"); на Свитче загрузчик
// отвечает своими функциями, на телефоне библиотеки нет и Available == false.
// Раскладка структур -- контракт с nx_bridge.h, меняется только вместе с
// NXB_VERSION.

using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace NxPort
{
    [StructLayout(LayoutKind.Sequential)]
    public struct NxbTouch { public uint down, id; public float x, y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct NxbPad
    {
        public uint version;
        public uint connected;
        public ulong buttons;
        public float lx, ly;
        public float rx, ry;
        public uint style;
        public uint docked;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NxbMotion
    {
        public uint valid;
        public float yaw;     // градусы вправо с прошлого вызова, уже с gyro_sens
        public float pitch;   // градусы вверх с прошлого вызова, уже с gyro_sens
        public float rate;
        public uint samples;
    }

    // Биты HidNpadButton из libnx.
    [Flags]
    public enum NxButton : ulong
    {
        A = 1UL << 0, B = 1UL << 1, X = 1UL << 2, Y = 1UL << 3,
        StickL = 1UL << 4, StickR = 1UL << 5,
        L = 1UL << 6, R = 1UL << 7, ZL = 1UL << 8, ZR = 1UL << 9,
        Plus = 1UL << 10, Minus = 1UL << 11,
        Left = 1UL << 12, Up = 1UL << 13, Right = 1UL << 14, Down = 1UL << 15,
    }

    public static class NxBridge
    {
        public const int Version = 1;
        const string Lib = "nxbridge";

        [DllImport(Lib)] static extern int nxb_version();
        [DllImport(Lib)] static extern int nxb_pad(ref NxbPad pad);
        [DllImport(Lib)] static extern int nxb_touch(ref NxbTouch touch);
        [DllImport(Lib)] static extern int nxb_config_set(string key, float value);
        [DllImport(Lib)] static extern int nxb_motion(ref NxbMotion motion);
        [DllImport(Lib)] static extern int nxb_config_int(string key);
        [DllImport(Lib)] static extern float nxb_config_float(string key);
        [DllImport(Lib)] static extern void nxb_log(string msg);
        [DllImport(Lib)] static extern void nxb_loading(int active);
        [DllImport(Lib)] static extern void nxb_fsr_applied(int width, int height, int outputWidth, int outputHeight, int active);

        static int s_state; // 0 не проверяли, 1 есть, -1 нет

        public static bool Available
        {
            get
            {
                if (s_state != 0) return s_state > 0;
                s_state = -1;
                if (Application.platform != RuntimePlatform.Android) return false;
                try
                {
                    int v = nxb_version();
                    if (v == Version) s_state = 1;
                    else Debug.LogWarning($"[NxPort] nxbridge v{v}, а слой ждёт v{Version}: выключен");
                }
                catch (Exception) { /* обычный Android: библиотеки нет */ }
                return s_state > 0;
            }
        }

        public static bool Pad(ref NxbPad pad) => Available && nxb_pad(ref pad) != 0;
        public static bool Touch(ref NxbTouch touch) => Available && nxb_touch(ref touch) != 0;
        public static bool SetConfig(string key, float value) => Available && nxb_config_set(key, value) != 0;
        public static bool Motion(ref NxbMotion m) => Available && nxb_motion(ref m) != 0;
        public static int ConfigInt(string key) => Available ? nxb_config_int(key) : 0;
        public static float ConfigFloat(string key) => Available ? nxb_config_float(key) : 0f;
        public static void Loading(bool active) { if (Available) nxb_loading(active ? 1 : 0); }
        public static void FsrApplied(int w, int h, int ow, int oh, bool active)
        { if (Available) nxb_fsr_applied(w, h, ow, oh, active ? 1 : 0); }

        public static void Log(string msg)
        {
            if (Available) nxb_log(msg);
            else Debug.Log("[NxPort] " + msg);
        }
    }
}
