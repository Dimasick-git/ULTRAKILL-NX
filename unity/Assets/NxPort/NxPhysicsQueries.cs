using UnityEngine;

namespace NxPort
{
    // Used only by the audited, read-only CameraFrustumTargeter scan. Never
    // disable automatic synchronization for a whole Update/physics phase:
    // other scripts move colliders between queries and need the original policy.
    public static class NxPhysicsQueries
    {
        public static bool BeginAimRead()
        {
            if (!Physics.autoSyncTransforms) return false;
            Physics.SyncTransforms();
            Physics.autoSyncTransforms = false;
            return true;
        }

        public static void EndAimRead(bool restore)
        {
            if (restore) Physics.autoSyncTransforms = true;
        }
    }
}
