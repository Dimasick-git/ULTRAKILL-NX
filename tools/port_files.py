#!/usr/bin/env python3
"""port_files.py -- шейдеры в сериализованных файлах плеера (без typetree) -> GLES3 на месте.

    python port_files.py <файл> [<файл> ...]

Для основных данных игры и встроенных ресурсов движка: globalgamemanagers.assets,
resources.assets, sharedassets0.assets, Resources/unity_builtin_extra,
Resources/unity default resources. В отличие от transplant_shaders.py шейдер
остаётся в своём же файле, поэтому ссылки на запасные шейдеры и текстуры
(m_Dependencies, m_NonModifiableTextures) верны и не трогаются.

Запись -- точечная (sfpatch), оригинал сохраняется рядом с .orig. Повторный
запуск берёт за основу .orig, так что результат не накапливается.
"""

import gc
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, r"C:\Users\dimas\uk-tools\pylib")
import UnityPy  # noqa: E402
import lean_tt  # noqa: E402
import shader_port  # noqa: E402
import sfpatch  # noqa: E402


def port_file(p):
    p = Path(p)
    backup = p.with_name(p.name + ".orig")
    if backup.exists():
        raw = backup.read_bytes()
        p.write_bytes(raw)
    else:
        raw = p.read_bytes()
    env = UnityPy.load(str(p))
    stats = {"programs": 0, "odd_type": 0}
    repl, notes = {}, []
    for obj in env.objects:
        if obj.type.name != "Shader":
            continue
        sh, _, _ = lean_tt.read(obj)
        name, note, changed = shader_port.convert_dict(sh, stats)
        if changed:
            repl[obj.path_id] = lean_tt.write(obj, sh)
        else:
            notes.append(f"{name}: {note}")
    del env
    gc.collect()
    if repl:
        if not backup.exists():
            backup.write_bytes(raw)
        p.write_bytes(sfpatch.replace_objects(raw, repl))
    print(f"{p.name}: шейдеров переведено {len(repl)}, программ {stats['programs']}")
    for n in notes[:10]:
        print("   ", n)


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    for f in sys.argv[1:]:
        port_file(f)


if __name__ == "__main__":
    main()
