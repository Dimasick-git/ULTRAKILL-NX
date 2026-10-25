// Reference-only checks for authored zero-scale shapes and frozen rigid bodies.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEditor;
using UnityEditor.SceneManagement;
public static class PhysicsShapeOracle {
 [Serializable] public class Sample {public Vector3 scale,boundsCenter,boundsSize,closestPoint,rayPointX,rayPointY,rayPointZ;public bool rayX,rayY,rayZ;public int overlaps;public string[] messages;}
 [Serializable] public class Frozen {public Vector3 beforeVelocity,beforeAngularVelocity,afterVelocity,afterAngularVelocity,beforePosition,afterPosition;public Quaternion beforeRotation,afterRotation;}
 [Serializable] public class Result {public string unityVersion;public Sample[] shapes;public Frozen frozen;}
 public static void Run(){
  Physics.autoSimulation=false;var results=new List<Sample>();
  foreach(var scale in new[]{Vector3.zero,new Vector3(0,1,0),new Vector3(1e-8f,1e-8f,1e-8f),new Vector3(.001f,.001f,.001f),Vector3.one}){
   var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);var messages=new List<string>();Application.LogCallback listener=(text,trace,type)=>{if(type==LogType.Warning||type==LogType.Error||type==LogType.Exception)messages.Add(type+": "+text);};Application.logMessageReceived+=listener;
   try{var go=new GameObject("scaled box");go.SetActive(false);go.transform.localScale=scale;var body=go.AddComponent<Rigidbody>();body.useGravity=false;var box=go.AddComponent<BoxCollider>();box.size=new Vector3(2,1.5f,1);box.center=new Vector3(-.62f,0,-.12f);go.SetActive(true);Physics.SyncTransforms();scene.GetPhysicsScene().Simulate(.008f);var sample=new Sample{scale=scale,boundsCenter=box.bounds.center,boundsSize=box.bounds.size,closestPoint=box.ClosestPoint(new Vector3(1,1,1))};RaycastHit hit;sample.rayX=Physics.Raycast(new Vector3(1,0,0),Vector3.left,out hit,2);sample.rayPointX=hit.point;sample.rayY=Physics.Raycast(new Vector3(0,1,0),Vector3.down,out hit,2);sample.rayPointY=hit.point;sample.rayZ=Physics.Raycast(new Vector3(0,0,1),Vector3.back,out hit,2);sample.rayPointZ=hit.point;sample.overlaps=Physics.OverlapSphere(Vector3.zero,1e-7f).Length;sample.messages=messages.ToArray();results.Add(sample);}finally{Application.logMessageReceived-=listener;foreach(var go in scene.GetRootGameObjects())UnityEngine.Object.DestroyImmediate(go);}
  }
  var frozenScene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);var frozenObject=new GameObject("frozen");frozenObject.transform.position=new Vector3(0,2,0);var rigid=frozenObject.AddComponent<Rigidbody>();rigid.constraints=RigidbodyConstraints.FreezeAll;frozenObject.AddComponent<BoxCollider>();Physics.SyncTransforms();rigid.velocity=new Vector3(10,-20,30);rigid.angularVelocity=new Vector3(1,2,3);var frozen=new Frozen{beforePosition=rigid.position,beforeRotation=rigid.rotation,beforeVelocity=rigid.velocity,beforeAngularVelocity=rigid.angularVelocity};frozenScene.GetPhysicsScene().Simulate(.008f);frozen.afterVelocity=rigid.velocity;frozen.afterAngularVelocity=rigid.angularVelocity;frozen.afterPosition=rigid.position;frozen.afterRotation=rigid.rotation;
  var path=Environment.GetEnvironmentVariable("NX_SHAPE_ORACLE_OUTPUT");if(string.IsNullOrEmpty(path))throw new Exception("NX_SHAPE_ORACLE_OUTPUT missing");File.WriteAllText(path,JsonUtility.ToJson(new Result{unityVersion=Application.unityVersion,shapes=results.ToArray(),frozen=frozen},true));Debug.Log("NX_SHAPE_ORACLE_PASS");EditorApplication.Exit(0);
 }
}
