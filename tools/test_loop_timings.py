"""Exercise production timing brackets with an instrumented native-loop model."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine.LowLevel;
namespace UnityEngine {
 public class MonoBehaviour {public bool enabled=true;}
 public static class Time {public static float unscaledTime;}
 public static class Physics {public static bool autoSyncTransforms=true;public static int Syncs;public static void SyncTransforms(){Syncs++;}}
}
namespace UnityEngine.LowLevel {
 public struct PlayerLoopSystem {
  public delegate void UpdateFunction();
  public Type type; public UpdateFunction updateDelegate; public IntPtr updateFunction,loopConditionFunction;public PlayerLoopSystem[] subSystemList;
 }
 public static class PlayerLoop {public static PlayerLoopSystem Current;public static PlayerLoopSystem GetCurrentPlayerLoop(){return Current;}public static void SetPlayerLoop(PlayerLoopSystem p){Current=p;}}
}
namespace UnityEngine.PlayerLoop {
 public struct FixedUpdate {public struct PhysicsFixedUpdate{} public struct ScriptRunBehaviourFixedUpdate{}}
 public struct Update {public struct ScriptRunBehaviourUpdate{}}
 public struct PostLateUpdate {public struct FinishFrameRendering{}}
}
namespace NxPort { public static class NxBridge {public static int Profile;public static int ConfigInt(string key){return Profile;}public static List<string> Logs=new List<string>();public static void Log(string s){Logs.Add(s);}} }
class Foreign {}
class Test {
 static int originalCalls;
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 static void Invoke(object o,string n){o.GetType().GetMethod(n,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(o,null);}
 static void Run(PlayerLoopSystem n){n.updateDelegate?.Invoke();if(n.subSystemList!=null)foreach(var c in n.subSystemList)Run(c);}
 static int Nodes(PlayerLoopSystem n){int count=1;if(n.subSystemList!=null)foreach(var c in n.subSystemList)count+=Nodes(c);return count;}
 static void Main(){
  var leaf=new PlayerLoopSystem {type=typeof(UnityEngine.PlayerLoop.FixedUpdate.PhysicsFixedUpdate),updateDelegate=()=>originalCalls++,updateFunction=new IntPtr(123),loopConditionFunction=new IntPtr(456)};
  PlayerLoop.Current=new PlayerLoopSystem {subSystemList=new[]{new PlayerLoopSystem {type=typeof(UnityEngine.PlayerLoop.FixedUpdate),subSystemList=new[]{leaf}}}};
  var timing=new NxPort.NxLoopTimings();Invoke(timing,"OnEnable");
  Check(!timing.enabled && Nodes(PlayerLoop.Current)==3,"normal play adds no timing nodes");
  Invoke(timing,"OnDisable");NxPort.NxBridge.Profile=1;timing.enabled=true;Invoke(timing,"OnEnable");
  Check(Nodes(PlayerLoop.Current)==5,"two brackets only");
  var retained=PlayerLoop.Current.subSystemList[0].subSystemList[1];
  Check(retained.type==leaf.type && retained.updateFunction==leaf.updateFunction && retained.loopConditionFunction==leaf.loopConditionFunction && retained.updateDelegate==leaf.updateDelegate,"native system unchanged");
  for(int i=0;i<1000;i++)Run(PlayerLoop.Current);
  Check(originalCalls==1000,"no skipped or duplicate native executions");
  UnityEngine.Time.unscaledTime=6;Invoke(timing,"Update");
  Check(NxPort.NxBridge.Logs.Exists(s=>s.Contains("calls=1000")),"all calls recorded");
  Invoke(timing,"OnEnable");Check(Nodes(PlayerLoop.Current)==5,"idempotent install");
  var changed=PlayerLoop.Current;var list=new List<PlayerLoopSystem>(changed.subSystemList);list.Add(new PlayerLoopSystem{type=typeof(Foreign)});changed.subSystemList=list.ToArray();PlayerLoop.Current=changed;
  Invoke(timing,"OnDisable");Check(Nodes(PlayerLoop.Current)==4,"remove only own nodes");Check(PlayerLoop.Current.subSystemList[1].type==typeof(Foreign),"other modifications retained");
  Run(PlayerLoop.Current);Check(originalCalls==1001,"native preserved after removal");
  Invoke(timing,"OnDisable");Check(Nodes(PlayerLoop.Current)==4,"double disable safe");
  PlayerLoop.Current=new PlayerLoopSystem();Invoke(timing,"OnEnable");Check(!timing.enabled,"unsupported loop disabled");
  Check(NxPort.NxPhysicsQueries.BeginAimRead(),"enabled policy captured");Check(!UnityEngine.Physics.autoSyncTransforms && UnityEngine.Physics.Syncs==1,"sync once then disable");
  Check(!NxPort.NxPhysicsQueries.BeginAimRead(),"nested scope retains false");NxPort.NxPhysicsQueries.EndAimRead(false);Check(!UnityEngine.Physics.autoSyncTransforms,"inner scope no restore");
  NxPort.NxPhysicsQueries.EndAimRead(true);Check(UnityEngine.Physics.autoSyncTransforms,"outer restores");
  UnityEngine.Physics.autoSyncTransforms=false;Check(!NxPort.NxPhysicsQueries.BeginAimRead(),"original false retained");NxPort.NxPhysicsQueries.EndAimRead(false);Check(!UnityEngine.Physics.autoSyncTransforms && UnityEngine.Physics.Syncs==1,"no extra sync when already disabled");
  Console.WriteLine("PASS: native loop ordering/pointers/1000 calls, idempotence, foreign nodes, disposal; production local physics policy/nesting");
 }
}
'''
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--mono',required=True);ap.add_argument('--csc',required=True);a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='nx-loop-test-') as d:
  d=Path(d);(d/'fixture.cs').write_text(FIXTURE)
  subprocess.run([a.mono,a.csc,'-nologo','-out:'+str(d/'test.exe'),str(d/'fixture.cs'),str(ROOT/'unity/Assets/NxPort/NxLoopTimings.cs'),str(ROOT/'unity/Assets/NxPort/NxPhysicsQueries.cs')],check=True)
  subprocess.run([a.mono,str(d/'test.exe')],check=True)
