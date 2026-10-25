"""Game-code fixes in the generated IL2CPP C++ (r75, built with -O2 instead of -Os).

Until r74 these lived in the loader as entry hooks on libil2cpp (nx_game_fixes.c),
tied to the exact code of the -Os build. A recompiled library has different code,
so the fixes move into the source:

1. BloodstainParent.ClearChildren: on level unload BloodsplatterManager.OnDestroy
   has already freed NativeArray props (m_Buffer == NULL) while bsm is still alive;
   the original wrote props[i] = default at 0 + i*32 (Data Abort, r64 1-2 -> 1-3).
2. BloodstainParent.RemoveStain: with stain recycling (bloodStainMax 512) the parent
   slot may belong to another surface; indexInParentList[index] points past its
   children list and List.set_Item throws (812 exceptions per session in r64).
   If children[pos] != index, only parentOfStain[index] = -1 is cleared.
3. CameraFrustumTargeter.Update: autoaim target search every N frames. The loader
   decides (nxb_aim_should_run: config autoaim_every, dead target -> search now).

Each insertion is checked against the expected original text and marked, so a
second run changes nothing and an unfamiliar export fails instead of guessing.
"""
from pathlib import Path
import argparse
import re

# r76: [cpu-scope] for the managed camera callbacks that cost Main Camera 3.9 ms
# in r75 (ScriptingInvocation::Invoke inside Camera::CustomRender). IDs continue
# the loader table in nx_cpu_scopes.c (names must stay in this order).
SCOPES = {
    "TMP_UpdateManager_DoRebuilds": 24,
    "PostProcessV2_Handler_OnPreRenderCallback": 25,
    "PortalManagerV2_OnPreRenderCallback": 26,
    "PostProcessV2_Handler_RenderLimboSkyboxes": 27,
    "PostProcessV2_Handler_RenderSpaceSky": 28,
    "PortalRenderV2_Render": 29,
    "NxFsr_Rendered": 30,
}
MARK_SCOPE = "// NX_CPU_SCOPE"

MARK_SKY = "// NX_SKY_THROTTLE"

MARK_CLEAR = "// NX_BLOODSTAIN_PROPS_GUARD"
MARK_REMOVE = "// NX_BLOODSTAIN_REMOVE_GUARD"
MARK_AIM = "// NX_AIM_THROTTLE"


def add_scopes(text):
    done = []
    for name, sid in SCOPES.items():
        m = re.search(r"^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR \w+ " + name + r"_m[0-9A-F]+ \([^\n]*\)\s*\n\{\n", text, re.M)
        if not m:
            continue
        if text[m.end():m.end() + 80].lstrip("\t").startswith("NxPortScope nx_scope("):
            continue
        text = text[:m.end()] + f"\tNxPortScope nx_scope({sid}); {MARK_SCOPE}\n" + text[m.end():]
        done.append(f"{name}#{sid}")
    if done and '#include "NxPortScope.h"' not in text:
        text = '#include "NxPortScope.h"\n' + text
    return text, done


def body(text, name):
    m = re.search(r"^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void " + name + r"_m[0-9A-F]+ \([^\n]*\)\s*\n\{\n", text, re.M)
    if not m:
        return None
    end = text.index("// Method Definition Index:", m.end())
    return m.end(), end


def patch(text):
    done = []
    span = body(text, "BloodstainParent_ClearChildren")
    if span and MARK_CLEAR not in text[span[0]:span[1]]:
        b = text[span[0]:span[1]]
        anchor = "IL_001c:\n\t{\n\t\tV_0 = 0;"
        assert b.count(anchor) == 1, "ClearChildren: anchor IL_001c not found"
        guard = ("IL_001c:\n\t{\n\t\tif (!__this->___bsm->___props.___m_Buffer) return; " + MARK_CLEAR +
                 "\n\t\tV_0 = 0;")
        text = text[:span[0]] + b.replace(anchor, guard, 1) + text[span[1]:]
        done.append("ClearChildren")
    span = body(text, "BloodstainParent_RemoveStain")
    if span and MARK_REMOVE not in text[span[0]:span[1]]:
        b = text[span[0]:span[1]]
        anchor = "\tint32_t V_0 = 0;\n\tint32_t V_1 = 0;\n\tint32_t V_2 = 0;\n"
        assert b.count(anchor) == 1, "RemoveStain: local variable block not found"
        guard = anchor + (
            "\t{ " + MARK_REMOVE + "\n"
            "\t\tauto* nx_bsm = __this->___bsm;\n"
            "\t\tauto* nx_children = __this->___children;\n"
            "\t\tif (nx_bsm && nx_children && nx_bsm->___indexInParentList && nx_bsm->___parentOfStain\n"
            "\t\t\t&& (il2cpp_array_size_t)(uint32_t)___0_index < nx_bsm->___indexInParentList->max_length) {\n"
            "\t\t\tint32_t nx_slot = nx_bsm->___indexInParentList->GetAt((il2cpp_array_size_t)___0_index);\n"
            "\t\t\tauto* nx_items = nx_children->____items;\n"
            "\t\t\tif (nx_slot < 0 || nx_slot >= nx_children->____size || !nx_items\n"
            "\t\t\t\t|| (il2cpp_array_size_t)nx_slot >= nx_items->max_length\n"
            "\t\t\t\t|| nx_items->GetAt((il2cpp_array_size_t)nx_slot) != ___0_index) {\n"
            "\t\t\t\tif ((il2cpp_array_size_t)(uint32_t)___0_index < nx_bsm->___parentOfStain->max_length)\n"
            "\t\t\t\t\tnx_bsm->___parentOfStain->SetAt((il2cpp_array_size_t)___0_index, (int32_t)(-1));\n"
            "\t\t\t\treturn;\n"
            "\t\t\t}\n"
            "\t\t}\n"
            "\t}\n")
        text = text[:span[0]] + b.replace(anchor, guard, 1) + text[span[1]:]
        done.append("RemoveStain")
    # 4. LimboSkybox.UpdateCamera: the Limbo "3D skybox" is rendered by hand
    #    (Camera.Render(fakeCam)) from the Main Camera's onPreRender, with the
    #    player's culling mask -- 2.9 ms per frame in r76 combat. The loader may
    #    skip the render (nxb_sky_should_render: sky_every, adaptive under 50 FPS);
    #    the RenderTexture keeps the previous frame's sky, transforms still update.
    span = body(text, "LimboSkybox_UpdateCamera")
    if span and MARK_SKY not in text[span[0]:span[1]]:
        b = text[span[0]:span[1]]
        m = re.search(r"\t\tCamera_Render_m[0-9A-F]+\(L_48, NULL\);\n", b)
        assert m and len(re.findall(r"Camera_Render_m[0-9A-F]+\(", b)) == 1, "LimboSkybox.UpdateCamera: Camera.Render not found"
        guard = ("\t\t{ " + MARK_SKY + "\n"
                 "\t\t\ttypedef int (*NxSkyShouldRender)(void*);\n"
                 "\t\t\tstatic NxSkyShouldRender nx_sky = (NxSkyShouldRender)dlsym(RTLD_DEFAULT, \"nxb_sky_should_render\");\n"
                 "\t\t\tif (nx_sky && !nx_sky(__this)) return;\n"
                 "\t\t}\n")
        b = b[:m.start()] + guard + b[m.start():]
        text = text[:span[0]] + b + text[span[1]:]
        if "<dlfcn.h>" not in text and '#include "NxPortScope.h"' not in text:
            text = "#include <dlfcn.h>\n" + text
        done.append("LimboSkybox.UpdateCamera")
    span = body(text, "CameraFrustumTargeter_Update")
    if span and MARK_AIM not in text[span[0]:span[1]]:
        b = text[span[0]:span[1]]
        assert b.startswith("\tNxPortScope nx_scope("), "CameraFrustumTargeter.Update: NX_CPU_SCOPE not at body start"
        guard = ("\t{ " + MARK_AIM + "\n"
                 "\t\ttypedef int (*NxAimShouldRun)(void*, void*);\n"
                 "\t\tstatic NxAimShouldRun nx_aim = (NxAimShouldRun)dlsym(RTLD_DEFAULT, \"nxb_aim_should_run\");\n"
                 "\t\tif (nx_aim && !nx_aim(__this, (void*)__this->___U3CCurrentTargetU3Ek__BackingField)) return;\n"
                 "\t}\n")
        text = text[:span[0]] + guard + b + text[span[1]:]
        done.append("CameraFrustumTargeter.Update")
    return text, done


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("cppdir", type=Path, help="Il2CppOutputProject/Source/il2cppOutput")
    a = p.parse_args()
    total = []
    files = sorted(a.cppdir.glob("Assembly-CSharp*.cpp")) + sorted(a.cppdir.glob("Unity.TextMeshPro*.cpp")) + \
        sorted(a.cppdir.glob("NxPort*.cpp"))
    for f in files:
        src = f.read_text(encoding="utf-8-sig")
        out, done = patch(src) if f.name.startswith("Assembly-CSharp") else (src, [])
        out, scoped = add_scopes(out)
        done += scoped
        if done:
            f.write_text(out, encoding="utf-8")
            print(f"{f.name}: {', '.join(done)}")
            total += done
    print("patched:", total or "nothing (already applied)")


if __name__ == "__main__":
    main()
