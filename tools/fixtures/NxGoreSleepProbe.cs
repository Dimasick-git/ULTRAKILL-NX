using System;
using System.Collections.Generic;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class NxGoreSleepProbe
{
    static void Check(bool value,string message) {if(!value) throw new Exception(message);}
    public static void Run()
    {
        var oldMode=Physics.simulationMode;
        var oldGravity=Physics.gravity;
        var objects=new List<GameObject>();
        try
        {
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
            Physics.simulationMode=SimulationMode.Script;
            Physics.gravity=new Vector3(0,-40,0);
            var bodies=new List<Rigidbody>();
            for(int i=0;i<64;i++)
            {
                var go=new GameObject("NX gore sleep probe "+i);objects.Add(go);
                var rb=go.AddComponent<Rigidbody>();rb.position=new Vector3(i*3,100,0);
                go.AddComponent<BoxCollider>();bodies.Add(rb);
                rb.velocity=Vector3.zero;rb.detectCollisions=false;rb.Sleep();
            }
            for(int tick=0;tick<125;tick++)Physics.Simulate(0.008f);
            for(int i=0;i<bodies.Count;i++)
            {
                var rb=bodies[i];
                Check(rb.IsSleeping() && (rb.position-new Vector3(i*3,100,0)).sqrMagnitude<0.000001f,"stopped body drifted under gravity");
                Check(!rb.isKinematic && rb.useGravity,"physics flags changed");
                // Original GoreSplatter.OnEnable resumes collisions and applies
                // a nonzero ForceMode.VelocityChange before setting rotation.
                rb.detectCollisions=true;
                rb.AddForce(new Vector3(20,0,0),ForceMode.VelocityChange);
                rb.rotation=Quaternion.Euler(10,20,30);
                Check(!rb.IsSleeping(),"pool launch did not wake body");
            }
            for(int tick=0;tick<10;tick++)Physics.Simulate(0.008f);
            for(int i=0;i<bodies.Count;i++)
            {
                var rb=bodies[i];Check(rb.position.x>i*3+0.5f && rb.position.y<100,"pool launch motion/gravity lost");
                rb.velocity=Vector3.zero;rb.detectCollisions=false;rb.Sleep();
            }
            var stopped=new List<Vector3>();foreach(var rb in bodies)stopped.Add(rb.position);
            for(int tick=0;tick<125;tick++)Physics.Simulate(0.008f);
            for(int i=0;i<bodies.Count;i++)Check(bodies[i].IsSleeping() && (bodies[i].position-stopped[i]).sqrMagnitude<0.000001f,"second stop failed");
            Debug.Log("[nx-gore-sleep-probe] PASS: 64 stopped dynamic bodies, 125 steps at 0.008 without drift; original collision/force/rotation pool wakeup; second stop; kinematic/gravity flags unchanged. Editor API behavior, not Switch FPS.");
        }
        finally
        {
            foreach(var go in objects)UnityEngine.Object.DestroyImmediate(go);
            Physics.gravity=oldGravity;Physics.simulationMode=oldMode;
        }
    }
}
