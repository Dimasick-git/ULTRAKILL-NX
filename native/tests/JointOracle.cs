// Reference-only Editor check; not part of the native runtime or game project.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEditor;
using UnityEditor.SceneManagement;

public static class JointOracle {
 [Serializable] public class Sample {public string name;public Vector3 requestedAxis,requestedSwing,storedSwing,connectedAnchor,normalizedAxis,normalizedSwing,initialAngularVelocity;public Quaternion before,after;public Vector3 position,angularVelocity;public string[] messages;}
 [Serializable] public class Result {public string unityVersion;public Sample[] samples;}
 public static void Run(){
  var samples=new List<Sample>();Physics.autoSimulation=false;
  foreach(var axis in new[]{Vector3.right,Vector3.left,Vector3.up,Vector3.down,Vector3.forward,Vector3.back,new Vector3(.2f,.3f,.9f).normalized,new Vector3(.8f,.3f,.1f).normalized}){
   var normalizedAxis=axis;var normalizedSwing=Vector3.zero;Vector3.OrthoNormalize(ref normalizedAxis,ref normalizedSwing);
   foreach(var swing in new[]{Vector3.zero,Vector3.right,Vector3.up,Vector3.forward,normalizedSwing}){
   if(swing!=Vector3.zero&&Mathf.Abs(Vector3.Dot(axis,swing))>.99f)continue;
   foreach(bool serialized in new[]{false,true}){
    foreach(float spin in new[]{0f,-10f,10f}){
    var messages=new List<string>();Application.LogCallback listener=(text,trace,type)=>{if(type==LogType.Warning||type==LogType.Error||type==LogType.Exception)messages.Add(type+": "+text);};Application.logMessageReceived+=listener;
    var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
    try{
     var parent=new GameObject("parent");SceneManager.MoveGameObjectToScene(parent,scene);parent.transform.position=new Vector3(0,2,0);var root=parent.AddComponent<Rigidbody>();root.isKinematic=true;
     var child=new GameObject("child");SceneManager.MoveGameObjectToScene(child,scene);child.SetActive(false);var rb=child.AddComponent<Rigidbody>();rb.useGravity=false;rb.mass=1;rb.angularDrag=0;rb.solverIterations=30;rb.solverVelocityIterations=5;child.AddComponent<SphereCollider>().radius=.1f;
     var joint=child.AddComponent<CharacterJoint>();joint.connectedBody=root;joint.axis=axis;joint.anchor=new Vector3(0,2,0);joint.autoConfigureConnectedAnchor=true;joint.lowTwistLimit=new SoftJointLimit{limit=-20};joint.highTwistLimit=new SoftJointLimit{limit=70};joint.swing1Limit=new SoftJointLimit{limit=30};joint.swing2Limit=new SoftJointLimit{limit=0};joint.enableCollision=false;joint.enableProjection=false;
     if(serialized){var so=new SerializedObject(joint);var property=so.FindProperty("m_SwingAxis");if(property==null)throw new Exception("m_SwingAxis property absent");property.vector3Value=swing;so.ApplyModifiedPropertiesWithoutUndo();}else joint.swingAxis=swing;
     child.SetActive(true);var sample=new Sample{name="axis-"+axis+"-swing-"+swing+(serialized?"-serialized":"-setter")+"-spin-"+spin,requestedAxis=axis,requestedSwing=swing,storedSwing=joint.swingAxis,normalizedAxis=normalizedAxis,normalizedSwing=normalizedSwing,connectedAnchor=joint.connectedAnchor,before=rb.rotation,initialAngularVelocity=spin==0?new Vector3(2,3,4):axis*spin};
     Physics.SyncTransforms();rb.angularVelocity=sample.initialAngularVelocity;var physics=scene.GetPhysicsScene();for(int i=0;i<125;i++)physics.Simulate(.008f);
     sample.after=rb.rotation;sample.position=rb.position;sample.angularVelocity=rb.angularVelocity;sample.messages=messages.ToArray();samples.Add(sample);
    }finally{foreach(var go in scene.GetRootGameObjects())UnityEngine.Object.DestroyImmediate(go);Application.logMessageReceived-=listener;}
   }
  }
  }
  }
  var path=Environment.GetEnvironmentVariable("NX_JOINT_ORACLE_OUTPUT");if(string.IsNullOrEmpty(path))throw new Exception("NX_JOINT_ORACLE_OUTPUT missing");File.WriteAllText(path,JsonUtility.ToJson(new Result{unityVersion=Application.unityVersion,samples=samples.ToArray()},true));Debug.Log("NX_JOINT_ORACLE_PASS "+samples.Count+" samples");EditorApplication.Exit(0);
 }
}
