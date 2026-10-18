#!/usr/bin/env python3
"""patch_apis.py -- список графических API в PlayerSettings (globalgamemanagers).

    python patch_apis.py <globalgamemanagers> [--target WindowsStandaloneSupport] [--api 11]

Плеер включает только те API, что записаны в PlayerSettings сборки; без GLES3
в списке «-force-gles32» падает с «was not built from editor». Меняем список у
нужной цели на один API (11 = OpenGLES3) и выключаем автоматический выбор.
Файл правится точечно (sfpatch), оригинал сохраняется рядом с .orig.
"""

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, r"C:\Users\dimas\uk-tools\pylib")
import UnityPy  # noqa: E402
import lean_tt  # noqa: E402
import sfpatch  # noqa: E402


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("ggm")
    ap.add_argument("--target", default="WindowsStandaloneSupport")
    ap.add_argument("--api", type=int, default=11)
    a = ap.parse_args()
    p = Path(a.ggm)
    raw = p.read_bytes()
    env = UnityPy.load(str(p))
    repl = {}
    for obj in env.objects:
        if obj.type.name == "BuildSettings":
            # в собранном плеере список API живёт здесь, а не в PlayerSettings
            v, _, _ = lean_tt.read(obj)
            print(f"  BuildSettings.m_GraphicsAPIs {list(v['m_GraphicsAPIs'])} -> [{a.api}]")
            v["m_GraphicsAPIs"] = [a.api]
            repl[obj.path_id] = lean_tt.write(obj, v)
            continue
        if obj.type.name != "PlayerSettings":
            continue
        v, _, _ = lean_tt.read(obj)
        apis = v.get("m_BuildTargetGraphicsAPIs") or []
        if not apis:
            continue
        for e in apis:
            print(f"  {e['m_BuildTarget']}: APIs {list(e['m_APIs'])} automatic={e['m_Automatic']}")
            if e["m_BuildTarget"] == a.target:
                e["m_APIs"] = [a.api]
                e["m_Automatic"] = False
                print(f"  -> {a.target}: [{a.api}]")
        repl[obj.path_id] = lean_tt.write(obj, v)
    del env
    if not repl:
        raise SystemExit("PlayerSettings не найден")
    backup = p.with_name(p.name + ".orig")
    if not backup.exists():
        backup.write_bytes(raw)
    p.write_bytes(sfpatch.replace_objects(raw, repl))
    print("записано")


if __name__ == "__main__":
    main()
