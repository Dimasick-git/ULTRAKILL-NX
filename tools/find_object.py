#!/usr/bin/env python3
"""find_object.py -- в каком бандле лежит CAB и что за объект по PathID.

    python find_object.py <папка бандлов> <CAB-имя> [pathid]
"""

import sys
from pathlib import Path

sys.path.insert(0, r"C:\Users\dimas\uk-tools\pylib")
import UnityPy  # noqa: E402

folder, cab = Path(sys.argv[1]), sys.argv[2].lower()
pid = int(sys.argv[3]) if len(sys.argv) > 3 else None

for f in sorted(folder.glob("*.bundle"), key=lambda p: p.stat().st_size):
    env = UnityPy.load(str(f))
    names = []
    for sf in env.files.values():
        names += list(getattr(sf, "files", {}).keys())
    if not any(cab in n.lower() for n in names):
        continue
    print(f"{cab} -> {f.name}")
    if pid is not None:
        for obj in env.objects:
            if obj.path_id == pid:
                d = obj.read()
                name = getattr(getattr(d, "m_ParsedForm", None), "m_Name", None) or getattr(d, "m_Name", "?")
                print(f"  pathid {pid}: {obj.type.name} '{name}'")
                if obj.type.name == "Shader":
                    pf = getattr(d, "platforms", None)
                    print(f"  platforms: {pf}")
    break
