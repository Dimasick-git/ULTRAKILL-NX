"""Production NxFsr lifecycle with instrumented Unity resource stubs.
Checks ownership/branching, not GPU output or Switch performance.
"""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
using System;
using System.Collections.Generic;
using System.Reflection;
namespace UnityEngine {
 public class Object {public bool destroyed;public string name;public static implicit operator bool(Object o){return o!=null&&!o.destroyed;}}
 public class DefaultExecutionOrder:Attribute {public DefaultExecutionOrder(int i){}}
 public class MonoBehaviour:Object {
  public Transform transform=new Transform();public static PostProcessV2_Handler Found;
  public static T FindObjectOfType<T>() where T:class {return Found as T;}
  public static void Destroy(Object o){if(o!=null)o.destroyed=true;}
 }
 public class Transform {public void SetParent(Transform p,bool world){}}
 public enum HideFlags{HideAndDontSave} public enum FilterMode{Point,Bilinear} public enum TextureWrapMode{Clamp}
 public enum RenderTextureFormat{ARGB32} public enum RenderTextureReadWrite{Default} public enum CameraClearFlags{Nothing} public enum ColorSpace{Gamma}
 public struct Vector2 {public static Vector2 zero {get{return new Vector2();}}}
 public struct Vector4 {public Vector4(float a,float b,float c,float d){}}
 public static class Mathf {public static int Clamp(int v,int a,int b){return Math.Max(a,Math.Min(b,v));}public static float Clamp(float v,float a,float b){return Math.Max(a,Math.Min(b,v));}public static float Pow(float a,float b){return (float)Math.Pow(a,b);}}
 public static class Time {public static int frameCount;public static float unscaledTime;}
 public static class Screen {public static int width=1280,height=720;public static void SetResolution(int w,int h,bool full){throw new Exception("Unexpected fallback");}}
 public static class QualitySettings {public static ColorSpace activeColorSpace=ColorSpace.Gamma;}
 public static class Application {public static string streamingAssetsPath="assets";}
 public class Shader:Object {public bool isSupported=true;}
 public class AssetBundle:Object {public static int Loads;public static AssetBundle LoadFromFile(string p){Loads++;return new AssetBundle();}public T LoadAsset<T>(string n) where T:new(){return new T();}public void Unload(bool all){}}
 public class Material:Object {public Material(Shader s){}public HideFlags hideFlags;public int passCount=2;public void SetVector(string s,Vector4 v){}}
 public class RenderTexture:Object {
  public static List<RenderTexture> All=new List<RenderTexture>();public int width,height;bool created;
  public FilterMode filterMode;public TextureWrapMode wrapMode;
  public RenderTexture(int w,int h,int d,RenderTextureFormat f,RenderTextureReadWrite r){width=w;height=h;All.Add(this);}
  public bool Create(){return created=true;}public bool IsCreated(){return created;}public void Release(){created=false;}
 }
 public class GameObject:Object {
  public static List<GameObject> All=new List<GameObject>();public Transform transform=new Transform();public HideFlags hideFlags;public bool activeInHierarchy=true;
  public GameObject(string n){name=n;All.Add(this);}public T AddComponent<T>() where T:new(){var c=new T();var camera=c as Camera;if(camera!=null)camera.gameObject=this;return c;}
 }
 public class Camera:Object {
  public static event Action<Camera> onPostRender;public static void Emit(Camera c){onPostRender?.Invoke(c);}
  public RenderTexture targetTexture;public float depth;public bool enabled=true,isActiveAndEnabled=true,allowHDR,allowMSAA,useOcclusionCulling;public int cullingMask;public CameraClearFlags clearFlags;
  public GameObject gameObject=new GameObject("game camera");
  public void AddCommandBuffer(Rendering.CameraEvent e,Rendering.CommandBuffer b){}public void RemoveCommandBuffer(Rendering.CameraEvent e,Rendering.CommandBuffer b){}
 }
}
namespace UnityEngine.Rendering {
 public enum CameraEvent{AfterEverything} public enum BuiltinRenderTextureType{CameraTarget}
 public class CommandBuffer {public static int Blits,Releases;public string name;public void BeginSample(string s){}public void EndSample(string s){}public void Release(){Releases++;}
  public void Blit(UnityEngine.RenderTexture a,UnityEngine.RenderTexture b,UnityEngine.Material m,int pass){Blits++;}
  public void Blit(UnityEngine.RenderTexture a,BuiltinRenderTextureType b,UnityEngine.Material m,int pass){Blits++;}
  public void Blit(UnityEngine.RenderTexture a,BuiltinRenderTextureType b){Blits++;}
 }
}
namespace UnityEngine.SceneManagement {
 public struct Scene{}public enum LoadSceneMode{Additive}
 public static class SceneManager {public static event Action<Scene,LoadSceneMode> sceneLoaded;public static event Action<Scene> sceneUnloaded;public static void Changed(){sceneUnloaded?.Invoke(new Scene());sceneLoaded?.Invoke(new Scene(),LoadSceneMode.Additive);}}
}
public class PostProcessV2_Handler:UnityEngine.Object {
 public bool isActiveAndEnabled=true;public int width=960,height=540;public float downscaleResolution=720;
 public UnityEngine.Camera mainCam=new UnityEngine.Camera(),hudCam=new UnityEngine.Camera(),virtualCam=new UnityEngine.Camera();
 public UnityEngine.RenderTexture mainTex,depthBuffer;
}
namespace NxPort {
 public static class NxBridge {
  public static bool filter;public static int Applied,LastHeight;public static int ConfigInt(string key){return key=="render_height"?540:key=="fsr_enabled"&&filter?1:0;}
  public static float ConfigFloat(string k){return .5f;}public static void Log(string s){}
  public static void FsrApplied(int w,int h,int ow,int oh,bool on){Applied++;LastHeight=h;}
 }
 public static class NxGameRenderTargets {
  public static int Rebuilds;public static void Rebuild(PostProcessV2_Handler p){Rebuilds++;p.height=(int)p.downscaleResolution;p.width=p.height*16/9;p.mainTex=new UnityEngine.RenderTexture(p.width,p.height,0,0,0);p.mainTex.Create();p.depthBuffer=new UnityEngine.RenderTexture(p.width,p.height,0,0,0);p.depthBuffer.Create();}
  public static bool NeedsRebuild(PostProcessV2_Handler p,int w,int h){return false;}
 }
 public class NxFsr3:IDisposable {public UnityEngine.Shader FrameGenerationShader;public bool TargetsCreated=true;public UnityEngine.Vector2 JitterOffset;
  public NxFsr3(PostProcessV2_Handler p,int w,int h,int ow,int oh){}public void Dispose(){}public void Rendered(UnityEngine.Camera c){}public void Compose(UnityEngine.Rendering.CommandBuffer b,UnityEngine.RenderTexture t,float s){}
 }
 public class NxFrameGeneration:IDisposable {public NxFrameGeneration(PostProcessV2_Handler p,UnityEngine.Camera c,int w,int h,int ow,int oh,UnityEngine.Shader s){}public void Dispose(){}public void Prepare(UnityEngine.Vector2 j){}
 }
}
class Test {
 static void Call(object o,string method){typeof(NxPort.NxFsr).GetMethod(method,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(o,null);}
 static void Check(bool v){if(!v)throw new Exception("Native composition regression");}
 static void Tick(NxPort.NxFsr port,int frame){UnityEngine.Time.frameCount=frame;UnityEngine.Time.unscaledTime=frame/10f;Call(port,"LateUpdate");}
 static int ExtraTargets(){int n=0;foreach(var t in UnityEngine.RenderTexture.All)if(!t.destroyed&&t.name!=null&&t.name.StartsWith("NX FSR"))n++;return n;}
 static int Displays(){int n=0;foreach(var g in UnityEngine.GameObject.All)if(!g.destroyed&&g.name=="NX FSR display")n++;return n;}
 static void Frame(PostProcessV2_Handler p){UnityEngine.Camera.Emit(p.mainCam);UnityEngine.Camera.Emit(p.virtualCam);}
 static void Main(){
  var post=new PostProcessV2_Handler();UnityEngine.MonoBehaviour.Found=post;var port=new NxPort.NxFsr();Call(port,"OnEnable");
  for(int i=1;i<=100;i++){Tick(port,i);Frame(post);}
  Check(ExtraTargets()==0 && Displays()==0 && UnityEngine.Rendering.CommandBuffer.Blits==0 && UnityEngine.AssetBundle.Loads==0);
  Check(NxPort.NxGameRenderTargets.Rebuilds==1 && NxPort.NxBridge.Applied==100 && NxPort.NxBridge.LastHeight==540 && post.virtualCam.targetTexture==null);
  UnityEngine.SceneManagement.SceneManager.Changed();Tick(port,101);Frame(post);Tick(port,102);Tick(port,106);Frame(post);
  Check(NxPort.NxGameRenderTargets.Rebuilds==3 && ExtraTargets()==0 && Displays()==0);
  NxPort.NxBridge.filter=true;Tick(port,110);
  Check(ExtraTargets()==2 && Displays()==1 && UnityEngine.Rendering.CommandBuffer.Blits==2 && post.virtualCam.targetTexture!=null && UnityEngine.AssetBundle.Loads==1);
  NxPort.NxBridge.filter=false;Tick(port,120);Frame(post);
  Check(ExtraTargets()==0 && Displays()==0 && post.virtualCam.targetTexture==null && UnityEngine.Rendering.CommandBuffer.Releases==1);
  NxPort.NxBridge.filter=true;Tick(port,130);Check(ExtraTargets()==2 && Displays()==1 && UnityEngine.AssetBundle.Loads==1);
  NxPort.NxBridge.filter=false;Tick(port,140);Check(ExtraTargets()==0 && Displays()==0 && UnityEngine.AssetBundle.Loads==1);
  Call(port,"OnDisable");int count=NxPort.NxBridge.Applied;Frame(post);Check(count==NxPort.NxBridge.Applied);
  Console.WriteLine("PASS: production OFF path loads no shader bundle and has no extra camera/FSR targets/blits; one scene-settle recovery; ON loads once, OFF releases targets, repeated ON reuses material; telemetry and unsubscribe intact (host ownership test)");
 }
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--mono',required=True);p.add_argument('--csc',required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory() as tmp:
  root=Path(tmp);fixture=root/'fixture.cs';exe=root/'test.exe';fixture.write_text(FIXTURE)
  subprocess.run([a.mono,a.csc,'/nologo','/out:'+str(exe),str(fixture),str(ROOT/'unity/Assets/NxPort/Fsr/NxFsr.cs'),str(ROOT/'unity/Assets/NxPort/Fsr/NxRenderTransition.cs')],check=True)
  subprocess.run([a.mono,str(exe)],check=True)
if __name__=='__main__':main()
