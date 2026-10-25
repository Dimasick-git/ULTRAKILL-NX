"""Exercise production managed reset ordering; not a Switch GPU simulation."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
using System;
namespace UnityEngine {
 public class Object {public static implicit operator bool(Object value){return value!=null;}}
 public struct RenderBuffer {public int id;}
 public struct Color {public static Color clear {get{return new Color();}}}
 public class RenderTexture:Object {
  public static RenderTexture active;public int width=320,height=180,id;public bool created=true;
  public bool IsCreated(){return created;}
  public RenderBuffer colorBuffer {get{return new RenderBuffer{id=id};}}
  public RenderBuffer depthBuffer {get{return new RenderBuffer{id=id+1000};}}
 }
 public class Camera:Object {
  public RenderTexture targetTexture;public int color,depth;
  public void SetTargetBuffers(RenderBuffer[] colors,RenderBuffer depthBuffer){color=colors[0].id;depth=depthBuffer.id;}
  public void SetTargetBuffers(RenderBuffer colorBuffer,RenderBuffer depthBuffer){color=colorBuffer.id;depth=depthBuffer.id;}
 }
 public static class Graphics {
  public static int color,depth;
  public static void SetRenderTarget(RenderBuffer c,RenderBuffer d){color=c.id;depth=d.id;RenderTexture.active=null;}
 }
 public static class GL {
  public static int clears;public static bool fail;
  public static void Clear(bool depth,bool color,Color value,float far){if(!depth||!color||far!=1f)throw new Exception("Wrong clear");clears++;if(fail)throw new Exception("Injected graphics failure");}
 }
}
public class PostProcessV2_Handler {
 public UnityEngine.Camera mainCam=new UnityEngine.Camera(),hudCam=new UnityEngine.Camera();
 public UnityEngine.RenderBuffer[] buffers=new UnityEngine.RenderBuffer[3],original;
 public UnityEngine.RenderTexture mainTex,depthBuffer,reusableBufferA,reusableBufferB,viewNormal;
 bool reinitializeTextures;public int setups;
 void SetupRTs(){
  if(!reinitializeTextures || mainCam.targetTexture!=null || hudCam.targetTexture!=null || object.ReferenceEquals(buffers,original))throw new Exception("Old attachment set survived until setup");
  setups++;mainTex=new UnityEngine.RenderTexture{id=setups*10};depthBuffer=new UnityEngine.RenderTexture{id=2};
  reusableBufferA=new UnityEngine.RenderTexture{id=3};reusableBufferB=new UnityEngine.RenderTexture{id=4};viewNormal=new UnityEngine.RenderTexture{id=5};reinitializeTextures=false;
 }
}
class Test {
 static void Check(bool value){if(!value)throw new Exception("Reset regression");}
 static void Main(){
  var post=new PostProcessV2_Handler();post.original=post.buffers;
  post.mainCam.targetTexture=post.hudCam.targetTexture=new UnityEngine.RenderTexture();
  var previous=new UnityEngine.RenderTexture{id=999};UnityEngine.RenderTexture.active=previous;
  NxPort.NxGameRenderTargets.Rebuild(post);
  Check(post.setups==1 && post.mainCam.color==10 && post.hudCam.color==10 && post.mainCam.depth==1002);
  Check(UnityEngine.GL.clears==1 && UnityEngine.Graphics.color==10 && UnityEngine.Graphics.depth==1002);
  Check(UnityEngine.RenderTexture.active==previous && !NxPort.NxGameRenderTargets.NeedsRebuild(post,320,180));
  post.viewNormal.created=false;Check(NxPort.NxGameRenderTargets.NeedsRebuild(post,320,180));
  post.original=post.buffers;UnityEngine.GL.fail=true;
  try{NxPort.NxGameRenderTargets.Rebuild(post);throw new Exception("Missing failure");}
  catch(Exception error){Check(error.Message=="Injected graphics failure");}
  Check(UnityEngine.RenderTexture.active==previous);
  Console.WriteLine("PASS: production reset detaches cameras before setup, replaces MRT array, binds fresh buffers, clears shared depth/color, restores active target on success/failure; host ordering only");
 }
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--mono',required=True);p.add_argument('--csc',required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory() as tmp:
  root=Path(tmp);fixture=root/'fixture.cs';exe=root/'test.exe';fixture.write_text(FIXTURE)
  subprocess.run([a.mono,a.csc,'/nologo','/out:'+str(exe),str(fixture),str(ROOT/'unity/Assets/NxPort/Fsr/NxGameRenderTargets.cs')],check=True)
  subprocess.run([a.mono,str(exe)],check=True)
if __name__=='__main__':main()
