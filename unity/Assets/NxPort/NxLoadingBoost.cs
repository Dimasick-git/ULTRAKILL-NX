using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace NxPort
{
    // Observe Unity's supported scene API instead of guessing from low FPS.
    public sealed class NxLoadingBoost : MonoBehaviour
    {
        sealed class LoadingAPI : SceneManagerAPI
        {
            readonly NxLoadingBoost owner;
            public LoadingAPI(NxLoadingBoost owner) { this.owner = owner; }
            protected override AsyncOperation LoadSceneAsyncByNameOrIndex(string name, int index,
                LoadSceneParameters parameters, bool nextFrame)
            {
                // Additive streaming may run during combat; FastLoad would
                // throttle its GPU even though no loading screen is active.
                if (parameters.loadSceneMode != LoadSceneMode.Single)
                    return base.LoadSceneAsyncByNameOrIndex(name, index, parameters, nextFrame);
                owner.Begin();
                try
                {
                    var operation = base.LoadSceneAsyncByNameOrIndex(name, index, parameters, nextFrame);
                    if (operation != null) owner.pending.Add(operation);
                    else owner.End();
                    return operation;
                }
                catch { owner.End(); throw; }
            }
        }
        readonly List<AsyncOperation> pending = new List<AsyncOperation>();
        LoadingAPI api;
        bool loading;
        void OnEnable()
        {
            NxBridge.Loading(false); // native startup/library load is complete
            if (SceneManagerAPI.overrideAPI == null)
                SceneManagerAPI.overrideAPI = api = new LoadingAPI(this);
            else NxBridge.Log("[loading] existing SceneManagerAPI retained; boost hook unavailable");
            SceneManager.sceneLoaded += Loaded;
        }
        void Begin() { loading = true; NxBridge.Loading(true); }
        void End() { loading = false; NxBridge.Loading(false); }
        void Loaded(Scene scene, LoadSceneMode mode)
        {
            // Do not retain FastLoad's GPU throttle into the first gameplay frame.
            pending.Clear(); End();
        }
        void Update()
        {
            pending.RemoveAll(operation => operation == null || operation.isDone);
            if (loading && pending.Count == 0) End();
        }
        void OnApplicationFocus(bool focus) { if (!focus) { pending.Clear(); End(); } }
        void OnDisable()
        {
            SceneManager.sceneLoaded -= Loaded;
            if (SceneManagerAPI.overrideAPI == api) SceneManagerAPI.overrideAPI = null;
            pending.Clear(); End();
        }
    }
}
