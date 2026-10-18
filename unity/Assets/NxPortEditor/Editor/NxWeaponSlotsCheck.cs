using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.Serialization;
using NxPort;
using UnityEditor;
using UnityEngine;

public static class NxWeaponSlotsCheck
{
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Export()
    {
        var marker = new GameObject("ExistingWeaponTest"); marker.SetActive(false);
        try
        {
            // Collection repair only accesses managed fields. Do not attach the
            // recovered game DLL's MonoScript to an Editor scene.
            var gun = (GunControl)FormatterServices.GetUninitializedObject(typeof(GunControl));
            const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
            var outer = typeof(GunControl).GetField("slots", flags);
            var fields = new FieldInfo[6];
            for (int i=0;i<6;++i) fields[i]=typeof(GunControl).GetField("slot"+(i+1),flags);
            Require(outer != null,"real GunControl outer field");
            foreach (var field in fields) Require(field!=null,"real GunControl slot field");
            foreach (var field in fields) field.SetValue(gun,null);
            outer.SetValue(gun,null);
            Require(NxWeaponSlots.Repair(gun)==7,"restore seven constructor invariants");
            Require(((List<List<GameObject>>)outer.GetValue(gun)).Count==0,"Start owns population");
            var kept=(List<GameObject>)fields[0].GetValue(gun);kept.Add(marker);
            var slots=new List<List<GameObject>> {kept,null}; outer.SetValue(gun,slots);
            fields[1].SetValue(gun,null);
            Require(NxWeaponSlots.Repair(gun)==2,"repair field and existing null entry");
            Require(ReferenceEquals(slots[0],kept) && kept.Count==1 && kept[0]==marker,"preserve existing weapon");
            Require(ReferenceEquals(slots[1],fields[1].GetValue(gun)) && slots.Count==2,"canonical slot, no duplicates");
            Require(NxWeaponSlots.Repair(gun)==0,"idempotent valid state");
            Debug.Log("[weapons-test] PASS: real GunControl schema, null collections, weapon preservation, no duplicate slots");
        }
        finally { UnityEngine.Object.DestroyImmediate(marker); }
        NxFsrBuild.Export();
    }
}
