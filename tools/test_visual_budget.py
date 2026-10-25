"""Run the production settings component against separate global/local prefs.

The stub deliberately has no scene search API. Validate lifecycle, no Update,
local setters/readers, migration, lower user limits and failure termination.
"""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
using System;
using System.Collections.Generic;
using System.Reflection;
namespace UnityEngine {
 public class Object {public static implicit operator bool(Object o){return o!=null;}}
 public class MonoBehaviour:Object {public bool enabled=true;}
 public class DefaultExecutionOrder:Attribute {public DefaultExecutionOrder(int x){}}
 public static class Mathf {public static float Min(float a,float b){return Math.Min(a,b);}}
}
public static class MonoSingleton<T> {public static T Instance;}
public class PrefsManager:UnityEngine.Object {
 public Dictionary<string,object> local=new Dictionary<string,object>();
 public int writes;public bool fail;
 public bool GetBoolLocal(string k,bool d){object v;return local.TryGetValue(k,out v)?(bool)v:d;}
 public float GetFloatLocal(string k,float d){object v;return local.TryGetValue(k,out v)?(float)v:d;}
 public void SetBoolLocal(string k,bool v){if(fail)throw new Exception("injected");++writes;local[k]=v;}
 public void SetFloatLocal(string k,float v){++writes;local[k]=v;}
}
namespace NxPort {public static class NxBridge {public static List<string> logs=new List<string>();public static void Log(string s){logs.Add(s);}}}
class Test {
 static void Check(bool ok){if(!ok)throw new Exception("assertion failed");}
 static NxPort.NxPerformance Boot(PrefsManager p){MonoSingleton<PrefsManager>.Instance=p;var c=new NxPort.NxPerformance();typeof(NxPort.NxPerformance).GetMethod("Awake",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(c,null);Check(!c.enabled);return c;}
 static void Main(){
  Check(typeof(NxPort.NxPerformance).GetMethod("Update",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)==null);
  Boot(null);Check(NxPort.NxBridge.logs.Count==1);
  var p=new PrefsManager();Boot(p);Check(p.writes==8&&p.GetBoolLocal("nxVisualBudgetV1",false));
  Check(p.GetFloatLocal("maxGore",-1)==100&&p.GetFloatLocal("bloodStainMax",-1)==512);
  Check(p.GetFloatLocal("bloodStainChance",-1)==0.2f);
  Check(p.GetBoolLocal("freezeGore",false)&&p.GetBoolLocal("simpleFire",false)&&p.GetBoolLocal("simpleExplosions",false)&&p.GetBoolLocal("disableEnvironmentParticles",false));
  p.local["simpleFire"]=false;Boot(p);Check(p.writes==8&&!p.GetBoolLocal("simpleFire",true));
  var lower=new PrefsManager();lower.local["maxGore"]=20f;lower.local["bloodStainMax"]=50f;lower.local["bloodStainChance"]=0f;Boot(lower);
  Check(lower.GetFloatLocal("maxGore",-1)==20&&lower.GetFloatLocal("bloodStainMax",-1)==50&&lower.GetFloatLocal("bloodStainChance",-1)==0);
  var bad=new PrefsManager{fail=true};Boot(bad);Check(!bad.GetBoolLocal("nxVisualBudgetV1",false)&&bad.writes==0);
  Check(NxPort.NxBridge.logs[NxPort.NxBridge.logs.Count-1].Contains("no retry loop"));
  Console.WriteLine("PASS: production Awake, no frame callback/scene search, local preference migration, lower limits, user edits, missing manager and injected failure");
 }
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--mono',required=True);ap.add_argument('--csc',required=True);a=ap.parse_args()
 with tempfile.TemporaryDirectory() as temp:
  p=Path(temp);fixture=p/'fixture.cs';fixture.write_text(FIXTURE);exe=p/'budget.exe'
  subprocess.run([a.mono,a.csc,'/nologo','/out:'+str(exe),str(fixture),str(ROOT/'unity/Assets/NxPort/NxPerformance.cs')],check=True)
  subprocess.run([a.mono,str(exe)],check=True)
if __name__=='__main__':main()
