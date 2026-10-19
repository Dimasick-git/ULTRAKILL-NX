#!/usr/bin/env python3
"""scan_bundles.py -- что внутри бандлов Addressables ULTRAKILL.

    python scan_bundles.py <папка aa/StandaloneWindows64> [-o scan.json] [--pylib DIR]

По каждому бандлу: платформа сериализованных файлов, счётчики типов,
имена шейдеров, форматы текстур, видео. Бандлы открываются по одному, чтобы
не держать всю игру в памяти (на этом ПК 7.7 ГБ).
"""

import argparse
import collections
import gc
import json
import os
import sys
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("-o", "--out", default="scan.json")
ap.add_argument(
    "--pylib",
    default=os.environ.get("UNITYPY_PATH"),
    help="каталог с UnityPy; по умолчанию используется установленный пакет",
)
a = ap.parse_args()
if a.pylib:
    sys.path.insert(0, a.pylib)
import UnityPy  # noqa: E402

TEXFMT = {}
try:
    from UnityPy.enums import TextureFormat
    TEXFMT = {e.value: e.name for e in TextureFormat}
except Exception:
    pass

report = {"bundles": {}, "shaders": collections.defaultdict(list),
          "texfmt": collections.Counter(), "platforms": collections.Counter()}

files = sorted(Path(a.dir).glob("*.bundle"), key=lambda p: p.stat().st_size)
for i, f in enumerate(files):
    env = UnityPy.load(str(f))
    types = collections.Counter()
    shaders, videos = [], []
    for sf_name, sf in env.files.items():
        tp = getattr(sf, "target_platform", None)
        if tp is not None:
            report["platforms"][str(tp)] += 1
    for obj in env.objects:
        t = obj.type.name
        types[t] += 1
        try:
            if t == "Shader":
                d = obj.read()
                name = getattr(getattr(d, "m_ParsedForm", None), "m_Name", None) or getattr(d, "m_Name", "?")
                shaders.append(name)
                report["shaders"][name].append(f.name)
            elif t == "Texture2D":
                d = obj.read()
                fmt = getattr(d, "m_TextureFormat", None)
                key = TEXFMT.get(int(fmt), str(fmt)) if fmt is not None else "?"
                report["texfmt"][key] += 1
            elif t == "VideoClip":
                d = obj.read()
                videos.append(getattr(d, "m_Name", "?"))
        except Exception as e:  # битый объект не должен ронять весь скан
            types["!read_error_" + t] += 1
    report["bundles"][f.name] = {"size": f.stat().st_size, "types": dict(types.most_common(12)),
                                 "shaders": shaders, "videos": videos}
    print(f"[{i + 1}/{len(files)}] {f.name}: {sum(types.values())} объектов, "
          f"шейдеров {len(shaders)}, видео {len(videos)}", flush=True)
    del env
    gc.collect()

report["shaders"] = dict(report["shaders"])
Path(a.out).write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
print("платформы:", dict(report["platforms"]))
print("форматы текстур:", dict(report["texfmt"].most_common()))
print(f"уникальных шейдеров: {len(report['shaders'])}")
