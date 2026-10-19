#!/usr/bin/env python3
"""probe_refs.py -- как материалы в бандлах ссылаются на шейдеры и где те лежат.

    python probe_refs.py <game ULTRAKILL_Data> [bundle-name]

Печатает шейдеры основных данных игры (globalgamemanagers.assets,
resources.assets, sharedassets0.assets) и для материалов из бандла --
файл, на который указывает их m_Shader (внешняя ссылка), и PathID.
"""

import collections
import sys
from pathlib import Path

try:
    import UnityPy
except ImportError as exc:
    raise SystemExit(
        "UnityPy не найден. Установите UnityPy или добавьте его в PYTHONPATH."
    ) from exc

data = Path(sys.argv[1])
bundle = sys.argv[2] if len(sys.argv) > 2 else "campaign_scenes_level1-1.bundle"

print("== шейдеры в основных данных")
for fn in ("globalgamemanagers.assets", "resources.assets", "sharedassets0.assets"):
    env = UnityPy.load(str(data / fn))
    for obj in env.objects:
        if obj.type.name == "Shader":
            d = obj.read()
            name = getattr(getattr(d, "m_ParsedForm", None), "m_Name", None) or getattr(d, "m_Name", "?")
            print(f"  {fn:28s} pathid={obj.path_id:<22d} {name}")

print(f"== материалы в {bundle}")
env = UnityPy.load(str(data / "StreamingAssets/aa/StandaloneWindows64" / bundle))
seen = collections.Counter()
for sf_name, sf in env.files.items():
    inner = getattr(sf, "files", {sf_name: sf})
    for nm, f in inner.items():
        ext = getattr(f, "externals", None)
        if ext is not None:
            print(f"  файл {nm}: внешних ссылок {len(ext)}")
            for i, e in enumerate(ext[:12]):
                print(f"    [{i + 1}] {getattr(e, 'path', e)}")
for obj in env.objects:
    if obj.type.name != "Material":
        continue
    d = obj.read()
    sh = d.m_Shader
    key = (sh.m_FileID, sh.m_PathID)
    if seen[key] == 0 and len(seen) < 25:
        print(f"  {getattr(d, 'm_Name', '?')[:40]:40s} shader fileid={sh.m_FileID} pathid={sh.m_PathID}")
    seen[key] += 1
print("  уникальных ссылок на шейдеры:", len(seen))
