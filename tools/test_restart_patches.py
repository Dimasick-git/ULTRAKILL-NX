"""Check safe cache reuse and the loading-scene audio guard."""
from pathlib import Path
import tempfile
from reuse_codegen_comments import reuse
from patch_audio_listener import patch
from patch_render_teardown import patch as teardown
from patch_fps_limit import patch as fps

def main():
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp);old=root/'old';new=root/'new';old.mkdir();new.mkdir()
        before=b'// Method Definition Index: 9\nint run(){return 41;}\n'
        after=before.replace(b'Index: 9',b'Index: 27')
        (old/'same.cpp').write_bytes(before);(new/'same.cpp').write_bytes(after)
        (old/'changed.cpp').write_bytes(before);(new/'changed.cpp').write_bytes(after.replace(b'return 41',b'return 42'))
        (old/'Il2CppMetadata.cpp').write_bytes(before);(new/'Il2CppMetadata.cpp').write_bytes(after)
        reuse(old,new)
        assert (new/'same.cpp').read_bytes()==before
        assert b'return 42' in (new/'changed.cpp').read_bytes()
        assert (new/'Il2CppMetadata.cpp').read_bytes()==after
        code='IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void VirtualAudioManager_UpdateSources_m'+('A'*40)+' (RuntimeObject* __this, float ___0_dt, float ___1_time, AudioListener_t123* ___2_audioListener, const RuntimeMethod* method)\n{\n\tcall_listener(___2_audioListener);\n}\n// Method Definition Index: 1\n'
        file=new/'Assembly-CSharp.cpp';file.write_text(code)
        patch(new);once=file.read_text();patch(new)
        assert once==file.read_text() and once.count('// NX_AUDIO_LISTENER_GUARD')==1
        assert once.index('if (!___2_audioListener) return;')<once.index('call_listener(')
        original='void PostProcessV2_Handler_ReleaseTextures_m'+('B'*40)+'(Post* __this, const RuntimeMethod* method);\n'
        original+='IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void PostProcessV2_Handler_OnDestroy_m'+('C'*40)+' (Post* __this, const RuntimeMethod* method)\n{\n\t\tunregister_camera();\n\t\treturn;\n}\n'
        changed,found=teardown(original);assert found and teardown(changed)[0]==changed
        assert changed.index('unregister_camera();')<changed.index('// NX_RENDER_OWNER_TEARDOWN')<changed.index('\t\treturn;')
        detach='NxGameRenderTargets_Detach_m'+('D'*40)
        detached,found=teardown(original,detach);assert found and teardown(detached,detach)[0]==detached
        assert detached.index('// NX_RENDER_DETACH')<detached.index('// NX_RENDER_OWNER_TEARDOWN')
        upgraded,found=teardown(changed,detach);assert found and '// NX_RENDER_DETACH' in upgraded
        setter='IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void GraphicsSettings_SetFrameRateLimit_m'+('E'*40)+' (Settings* __this, int value)\n{\n\t\tApplication_set_targetFrameRate_m'+('F'*40)+'(V_0, NULL);\n\t\treturn;\n}\n'
        immediate,found=fps(setter);assert found and fps(immediate)[0]==immediate
        assert immediate.index('Application_set_targetFrameRate_')<immediate.index('// NX_FPS_MENU_REQUEST')<immediate.index('return;')
        temporary,found=fps(setter.replace('V_0, NULL','L_5, NULL'));assert found and 'request(L_5)' in temporary
    print('PASS: comment-only code reuse, changed code and metadata retained, audio listener guard idempotent and before dereference')
if __name__=='__main__':main()
