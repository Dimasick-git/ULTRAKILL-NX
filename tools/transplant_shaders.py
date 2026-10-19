#!/usr/bin/env python3
"""transplant_shaders.py -- шейдеры основных данных собранного плеера -> оригиналы игры в GLES3.

    python transplant_shaders.py <ULTRAKILL_Data игры> <Data собранного плеера>

В проекте ukport шейдеры основных данных (TextMeshPro, UI Extensions, Standard,
Skybox...) -- заглушки AssetRipper: свойства верные, а код «текстура × цвет».
После сборки каждый такой шейдер в resources.assets / sharedassets0.assets /
globalgamemanagers.assets плеера заменяется настоящим шейдером игры с тем же
именем, переведённым в GLES3 (shader_port.convert_dict).

Ссылки внутри шейдера (m_Dependencies -- запасные шейдеры, m_NonModifiableTextures)
указывают на объекты файлов игры, а не плеера, поэтому обнуляются: программы
GLES3 есть у всех вариантов, до запасного шейдера дело не доходит.
"""

import gc
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import UnityPy  # noqa: E402
import lean_tt  # noqa: E402
import shader_port  # noqa: E402
import sfpatch  # noqa: E402

FILES = ("globalgamemanagers.assets", "resources.assets", "sharedassets0.assets")


def originals(game_data):
    """имя шейдера -> переведённое значение (dict) из файлов игры."""
    out = {}
    stats = {"programs": 0, "odd_type": 0}
    for fn in FILES:
        p = Path(game_data) / fn
        if not p.exists():
            continue
        env = UnityPy.load(str(p))
        for obj in env.objects:
            if obj.type.name != "Shader":
                continue
            sh, _, _ = lean_tt.read(obj)
            name, note, changed = shader_port.convert_dict(sh, stats)
            if changed or note == "уже GLES3":
                sh["m_Dependencies"] = []
                sh["m_NonModifiableTextures"] = []
                if isinstance(sh.get("compressedBlob"), memoryview):
                    sh["compressedBlob"] = bytes(sh["compressedBlob"])
                out[name] = sh
        del env
        gc.collect()
    print(f"оригиналов: {len(out)}, программ {stats['programs']}")
    return out


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    game_data, player_data = Path(sys.argv[1]), Path(sys.argv[2])
    orig = originals(game_data)
    for fn in FILES:
        p = player_data / fn
        if not p.exists():
            continue
        raw = p.read_bytes()
        env = UnityPy.load(str(p))
        n = 0
        repl = {}
        for obj in env.objects:
            if obj.type.name != "Shader":
                continue
            cur, _, _ = lean_tt.read(obj)
            name = cur["m_ParsedForm"]["m_Name"]
            src = orig.get(name)
            if src is None:
                print(f"  {fn}: {name} -- в игре такого нет, оставлен")
                continue
            repl[obj.path_id] = lean_tt.write(obj, src)
            n += 1
            print(f"  {fn}: {name} -> оригинал (GLES3)")
        del env
        gc.collect()
        if n:
            # UnityPy ломает служебную часть файла при сохранении, поэтому только
            # точечная замена объектов (sfpatch); оригинал -- рядом с .orig
            backup = p.with_name(p.name + ".orig")
            if not backup.exists():
                backup.write_bytes(raw)
            p.write_bytes(sfpatch.replace_objects(raw, repl))
        print(f"{fn}: заменено {n}")


if __name__ == "__main__":
    main()
