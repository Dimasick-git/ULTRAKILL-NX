namespace NxPort
{
    // One recovery per scene transition, after the new scene's cameras have
    // actually completed a world/composition frame. No periodic rebuild loop.
    public sealed class NxRenderTransition
    {
        bool pending;
        int presented = -1;
        public void SceneChanged() { pending = true; presented = -1; }
        public void Presented(int frame, bool worldRendered)
        { if (pending && presented < 0 && worldRendered) presented = frame; }
        public bool TakeReset(int frame)
        {
            if (!pending || presented < 0 || frame <= presented) return false;
            pending = false; presented = -1; return true;
        }
    }
}
