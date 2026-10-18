// NxPortBootstrap.cs -- поднимает слой порта при старте игры.
//
// Срабатывает только на Свитче (есть nxbridge от загрузчика ultrakill_nx). На
// ПК и обычном Android ничего не делает, так что этот же проект собирается и
// запускается как раньше.

using UnityEngine;

namespace NxPort
{
    public static class NxPortBootstrap
    {
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void Init()
        {
            if (!NxBridge.Available) return;

            var go = new GameObject("NxPort");
            go.hideFlags = HideFlags.HideAndDontSave;   // смена сцены его не уничтожит
            Object.DontDestroyOnLoad(go);
            go.AddComponent<NxGamepad>();
            go.AddComponent<NxVideoManager>();
            go.AddComponent<NxWeaponSlots>();
            if (NxBridge.ConfigInt("fsr") != 0) go.AddComponent<NxFsr>();
            if (NxBridge.ConfigInt("gyro") != 0) go.AddComponent<NxGyroAim>();

            NxBridge.Log($"слой порта поднят: gyro={NxBridge.ConfigInt("gyro")} " +
                         $"sens={NxBridge.ConfigFloat("gyro_sens"):0.00}");
        }
    }
}
