from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
using System;
class Test {
 static void Check(bool condition){if(!condition)throw new Exception("Transition reset regression");}
 static void Main(){
  var state=new NxPort.NxRenderTransition();
  for(int i=0;i<100;i++){state.Presented(i,true);Check(!state.TakeReset(i+1));}
  state.SceneChanged();
  for(int i=100;i<200;i++){state.Presented(i,false);Check(!state.TakeReset(i+1));}
  state.Presented(200,true);Check(!state.TakeReset(200));Check(state.TakeReset(201));
  for(int i=201;i<10000;i++){state.Presented(i,true);Check(!state.TakeReset(i+1));}
  state.SceneChanged();state.Presented(10000,true);state.SceneChanged();
  Check(!state.TakeReset(10001)); // unload/load invalidates a previous scene's acknowledgment
  state.Presented(10001,true);state.Presented(10002,true);Check(state.TakeReset(10003));
  Check(!state.TakeReset(10004));
  Console.WriteLine("PASS: no reset before world presentation; next-frame one-shot; scene change invalidates old acknowledgment; no periodic reset loop");
 }
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--mono',required=True);p.add_argument('--csc',required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory() as tmp:
  root=Path(tmp);fixture=root/'test.cs';exe=root/'test.exe';fixture.write_text(FIXTURE)
  subprocess.run([a.mono,a.csc,'/nologo','/out:'+str(exe),str(fixture),str(ROOT/'unity/Assets/NxPort/Fsr/NxRenderTransition.cs')],check=True)
  subprocess.run([a.mono,str(exe)],check=True)
if __name__=='__main__':main()
