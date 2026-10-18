#!/usr/bin/env python3
"""fix_catalog.py -- catalog.json Addressables: пути к бандлам с прямыми слешами.

    python fix_catalog.py <aa/catalog.json> [--check]

Каталог собран под Windows: внутренние пути вида
"{...RuntimePath}\\StandaloneWindows64\\x.bundle". На Android RuntimePath --
«jar:file://...!/assets/aa», а с обратными слешами такой путь в APK не найти.
Меняем «\\» на «/» во всех m_InternalIds; остальное не трогаем.
"""

import json
import sys
from pathlib import Path

BS = chr(92)  # обратный слеш, без экранирования в исходнике


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    p = Path(sys.argv[1])
    c = json.loads(p.read_text(encoding="utf-8"))
    ids = c["m_InternalIds"]
    bad = sum(1 for i in ids if BS in i)
    bundles = [i for i in ids if i.endswith(".bundle")]
    print(f"internal ids: {len(ids)}, с обратным слешем: {bad}, бандлов: {len(bundles)}")
    print("  пример:", bundles[:2])
    if "--check" in sys.argv:
        return
    c["m_InternalIds"] = [i.replace(BS, "/") for i in ids]
    p.write_text(json.dumps(c, separators=(",", ":"), ensure_ascii=False), encoding="utf-8")
    print("  после:", [i for i in c["m_InternalIds"] if i.endswith(".bundle")][:2])


if __name__ == "__main__":
    main()
