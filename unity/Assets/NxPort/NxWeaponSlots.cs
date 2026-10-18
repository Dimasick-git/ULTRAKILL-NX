using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace NxPort
{
    // Restore the non-null collection invariants established by GunControl's
    // original constructor. No weapons are created, unlocked or removed.
    public sealed class NxWeaponSlots : MonoBehaviour
    {
        const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        static readonly FieldInfo Slots = typeof(GunControl).GetField("slots", Flags);
        static readonly FieldInfo[] Fields = {
            typeof(GunControl).GetField("slot1", Flags), typeof(GunControl).GetField("slot2", Flags),
            typeof(GunControl).GetField("slot3", Flags), typeof(GunControl).GetField("slot4", Flags),
            typeof(GunControl).GetField("slot5", Flags), typeof(GunControl).GetField("slot6", Flags)
        };

        public static int Repair(GunControl control)
        {
            if (Slots == null) return 0;
            foreach (var field in Fields) if (field == null) return 0;
            int repairs = 0;
            var canonical = new List<GameObject>[6];
            for (int i = 0; i < 6; ++i)
            {
                canonical[i] = Fields[i].GetValue(control) as List<GameObject>;
                if (canonical[i] != null) continue;
                canonical[i] = new List<GameObject>();
                Fields[i].SetValue(control, canonical[i]); ++repairs;
            }
            var slots = Slots.GetValue(control) as List<List<GameObject>>;
            if (slots == null)
            {
                // Start adds the six lists itself. Do not pre-populate/duplicate them.
                Slots.SetValue(control, new List<List<GameObject>>(6)); ++repairs;
            }
            else for (int i = 0; i < slots.Count && i < 6; ++i)
            {
                if (slots[i] != null) continue;
                slots[i] = canonical[i]; ++repairs;
            }
            return repairs;
        }

        void Awake()
        {
            SceneManager.sceneLoaded += Loaded;
            Loaded(default, LoadSceneMode.Single);
        }
        void OnDestroy() { SceneManager.sceneLoaded -= Loaded; }
        void Loaded(Scene scene, LoadSceneMode mode)
        {
            int count = 0;
            foreach (var control in Resources.FindObjectsOfTypeAll<GunControl>())
                if (control.gameObject.scene.IsValid()) count += Repair(control);
            NxBridge.Log("[weapons] scene collection repairs=" + count);
        }
    }
}
