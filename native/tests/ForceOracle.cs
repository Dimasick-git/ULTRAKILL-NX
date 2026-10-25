// Editor-only reference measurements. Not linked or distributed with the game.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEditor;
using UnityEditor.SceneManagement;
public static class ForceOracle {
 [Serializable] public class Sample {public string scenario;public int mode;public float massBefore,massAfter;public Vector3 before,immediate,after,second;}
 [Serializable] public class Result {public string unityVersion;public Sample[] samples;}
 public static void Run(){
  Physics.autoSimulation=false;var output=new List<Sample>();
  foreach(float mass in new[]{2f,7f})foreach(ForceMode mode in new[]{ForceMode.Force,ForceMode.Impulse,ForceMode.VelocityChange,ForceMode.Acceleration})foreach(string scenario in new[]{"normal","write-after","mass-after","deactivate","mode-cycle","inactive-call","kinematic-call"}){
   var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);var go=new GameObject("force reference");go.transform.position=new Vector3(0,100,0);go.AddComponent<BoxCollider>();var body=go.AddComponent<Rigidbody>();body.mass=mass;body.useGravity=false;body.drag=0;body.angularDrag=0;Physics.SyncTransforms();body.velocity=new Vector3(1,2,3);
   var sample=new Sample{scenario=scenario,mode=(int)mode,massBefore=mass,before=body.velocity};
   if(scenario=="inactive-call")go.SetActive(false);if(scenario=="kinematic-call")body.isKinematic=true;
   body.AddForce(new Vector3(10,20,-30),mode);
   if(scenario=="write-after")body.velocity=new Vector3(4,5,6);
   if(scenario=="mass-after")body.mass=mass*2;
   if(scenario=="deactivate"){go.SetActive(false);go.SetActive(true);}
   if(scenario=="mode-cycle"){body.isKinematic=true;body.isKinematic=false;}
   if(scenario=="inactive-call")go.SetActive(true);if(scenario=="kinematic-call")body.isKinematic=false;
   sample.massAfter=body.mass;sample.immediate=body.velocity;scene.GetPhysicsScene().Simulate(.008f);sample.after=body.velocity;scene.GetPhysicsScene().Simulate(.008f);sample.second=body.velocity;output.Add(sample);UnityEngine.Object.DestroyImmediate(go);
  }
  var path=Environment.GetEnvironmentVariable("NX_FORCE_ORACLE_OUTPUT");if(string.IsNullOrEmpty(path))throw new Exception("NX_FORCE_ORACLE_OUTPUT missing");File.WriteAllText(path,JsonUtility.ToJson(new Result{unityVersion=Application.unityVersion,samples=output.ToArray()},true));Debug.Log("NX_FORCE_ORACLE_PASS "+output.Count);EditorApplication.Exit(0);
 }
}
