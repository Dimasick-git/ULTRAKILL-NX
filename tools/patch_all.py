#!/usr/bin/env python3
"""Все правки IL2CPP после выгрузки Unity (NxBuild.AndroidExport), по порядку.

Выгрузка заново копирует рантайм IL2CPP и генерирует весь C++, поэтому правки
накладываются каждый раз: сначала рантайм (GC, трассы null/bounds), затем код
игры и движка, последней -- локализация. Порядок важен: NX_AIM_THROTTLE
(patch_nx_game_fixes) ставится после NX_CPU_SCOPE (patch_cpu_scopes).

    python tools/patch_all.py <unityLibrary/src/main/Il2CppOutputProject>
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def find(gen, function, prefer=None):
    """Файл с определением function; при нескольких -- тот, где есть prefer
    (метка уже стоящей правки или место, которое правка меняет)."""
    hits = [p for p in sorted(gen.glob("Assembly-CSharp*.cpp"))
            if re.search(r"IL2CPP_METHOD_ATTR void " + function + r"_m[0-9A-F]+ \(", p.read_text(encoding="utf-8", errors="ignore"))]
    if len(hits) > 1 and prefer:
        hits = [p for p in hits if re.search(prefer, p.read_text(encoding="utf-8", errors="ignore"))][:1]
    if len(hits) != 1:
        raise SystemExit(f"{function}: ожидался один файл, найдено {len(hits)}")
    return hits[0]


def run(tool, *args):
    cmd = [sys.executable, "-X", "utf8", str(HERE / tool), *map(str, args)]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = (r.stdout + r.stderr).strip().splitlines()
    print(f"{tool}: {'OK' if r.returncode == 0 else 'ОШИБКА'} -- {out[-1] if out else ''}")
    if r.returncode != 0:
        print("\n".join(out[-20:]))
        raise SystemExit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project", type=Path, help="Il2CppOutputProject")
    a = ap.parse_args()
    rt, gen = a.project / "IL2CPP", a.project / "Source" / "il2cppOutput"
    run("patch_nullcheck_site.py", rt)
    run("patch_bounds_site.py", rt)
    run("patch_gc_horizon.py", rt / "external" / "bdwgc" / "pthread_stop_world.c")
    run("patch_exception_trace.py", rt / "libil2cpp" / "vm" / "Exception.cpp")
    run("patch_gc_no_unmap.py", rt / "external" / "bdwgc" / "allchblk.c")
    run("patch_mouse_scroll.py", gen)
    run("patch_world_mrt.py", find(gen, "PostProcessV2_Handler_OnPreRenderCallback"))
    run("patch_aim_query_order.py", find(gen, "CameraFrustumTargeter_Update"))
    run("patch_stain_transfer.py", find(gen, "BloodstainParent_AddStain",
                                        r"NX_STAIN_TRANSFER_MISSING_OLD_PARENT|"
                                        r"NullCheck\(L_10\);\n\t\tBloodstainParent_RemoveStain_m\w+\(L_10, L_11, NULL\);"))
    run("patch_collision_contacts.py", gen)
    run("patch_aim_sync_batch.py", gen)
    run("patch_gore_sleep.py", gen)
    run("patch_cpu_scopes.py", gen)
    run("patch_nx_game_fixes.py", gen)
    run("patch_nx_localization.py", gen)
    run("patch_scene_memory.py", gen)
    run("patch_audio_listener.py", gen)
    run("patch_render_teardown.py", gen)
    run("patch_fps_limit.py", gen)
    print("все правки на месте")


if __name__ == "__main__":
    main()
