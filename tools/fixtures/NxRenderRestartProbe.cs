using System;
using NxPort;
using UnityEditor;
using UnityEngine;

// Runs in the small render fixture, with the original SetupRTs allocation
// contract represented by its stub. Uses real Unity GPU textures and cameras.
public static class NxRenderRestartProbe
{
    static void Check(bool value,string message) { if(!value)throw new Exception(message); }
    static GameObject CameraRig()
    {
        var root=new GameObject("Main Camera",typeof(Camera));root.GetComponent<Camera>().enabled=false;
        foreach(var name in new[]{"HUD Camera","Virtual Camera"}) {
            var child=new GameObject(name,typeof(Camera));child.transform.SetParent(root.transform,false);child.GetComponent<Camera>().enabled=false;
        }
        return root;
    }
    public static void Run()
    {
        var rig=CameraRig();var owner=new GameObject("Compositor",typeof(PostProcessV2_Handler));
        var post=owner.GetComponent<PostProcessV2_Handler>();var pixel=new Texture2D(1,1,TextureFormat.RGBA32,false);
        try
        {
            Check(NxGameRenderTargets.SyncCameras(post,rig.GetComponent<Camera>()),"initial camera rig discovery failed");
            for(int cycle=0;cycle<32;cycle++) {
                var old=post.mainTex;bool hadOld=old;PostProcessV2_Handler.ExpectedOld=old;
                if(hadOld)RenderTexture.active=old;
                NxGameRenderTargets.Rebuild(post);
                Check(!hadOld||PostProcessV2_Handler.OldSurvivedAllocation,"old backing released before new set allocated");
                Check(!RenderTexture.active,"retired render target remained active");
                Check(!NxGameRenderTargets.NeedsRebuild(post,64,64),"new target set incomplete");
                var previous=RenderTexture.active;RenderTexture.active=post.mainTex;
                pixel.ReadPixels(new Rect(32,32,1,1),0,0);pixel.Apply();RenderTexture.active=previous;
                Check(pixel.GetPixel(0,0).a<0.01f,"GPU clear of fresh MRT did not execute");
            }
            var healthy=post.mainTex;PostProcessV2_Handler.ExpectedOld=healthy;PostProcessV2_Handler.ThrowAfterFirst=true;
            bool threw=false;try {NxGameRenderTargets.Rebuild(post);}catch {threw=true;}
            Check(threw&&post.mainTex==healthy&&healthy.IsCreated(),"partial failure did not restore the old set");
            PostProcessV2_Handler.ThrowAfterFirst=false;
            UnityEngine.Object.DestroyImmediate(rig);rig=CameraRig();
            Check(NxGameRenderTargets.SyncCameras(post,rig.GetComponent<Camera>()),"restart camera discovery failed");
            Check(post.hudCam.transform.parent==rig.transform&&post.virtualCam.transform.parent==rig.transform,"stale HUD/output references survived restart");
            PostProcessV2_Handler.ExpectedOld=healthy;NxGameRenderTargets.Rebuild(post);
            RenderTexture.active=post.mainTex;NxGameRenderTargets.Detach(post);
            Check(!RenderTexture.active,"teardown left an owned render target active");
            NxGameRenderTargets.Reattach(post);
            Debug.Log("[nx-render-restart] PASS: 32 same-size GPU rebuilds, old backing retained through allocation, partial-failure rollback, fresh HUD/output cameras after restart");
            EditorApplication.Exit(0);
        }
        catch(Exception error) {Debug.LogException(error);EditorApplication.Exit(1);}
        finally
        {
            if(post){post.mainCam.targetTexture=null;post.hudCam.targetTexture=null;
                foreach(var rt in new[]{post.mainTex,post.reusableBufferA,post.reusableBufferB,post.depthBuffer,post.viewNormal})
                    if(rt){rt.Release();UnityEngine.Object.DestroyImmediate(rt);}}
            UnityEngine.Object.DestroyImmediate(pixel);UnityEngine.Object.DestroyImmediate(rig);UnityEngine.Object.DestroyImmediate(owner);
        }
    }
}
