#!/usr/bin/env python3
"""prep_data.py -- основные данные игры (Windows) -> данные для Android-плеера.

    python prep_data.py <ULTRAKILL_Data игры> <выход>

Плеер Android 2022.3.29f1 (та же версия, что у игры) принимает оригинальные
основные данные, если в них:
  * шейдеры переведены в GLES3 (port_files.py; встроенные ресурсы движка берутся
    из нашей Android-сборки, их не трогаем);
  * в BuildSettings.m_GraphicsAPIs стоит только 11 (OpenGLES3) -- patch_apis.py;
  * в заголовке каждого сериализованного файла платформа 13 (Android) вместо 19.
"""

import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FILES = ["globalgamemanagers", "globalgamemanagers.assets", "globalgamemanagers.assets.resS",
         "level0", "level0.resS", "resources.assets", "resources.assets.resS",
         "sharedassets0.assets", "sharedassets0.assets.resS"]
SERIALIZED = ["globalgamemanagers", "globalgamemanagers.assets", "level0",
              "resources.assets", "sharedassets0.assets"]
ANDROID = 13


def set_platform(path, platform):
    d = bytearray(Path(path).read_bytes())
    version = struct.unpack_from(">I", d, 8)[0]
    if version < 22:
        raise SystemExit(f"{path}: формат {version}")
    e = "<" if d[16] == 0 else ">"
    p = d.index(b"\0", 48) + 1           # после строки версии Unity
    old = struct.unpack_from(e + "i", d, p)[0]
    struct.pack_into(e + "i", d, p, platform)
    Path(path).write_bytes(bytes(d))
    return old


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    for f in FILES:
        if (src / f).exists():
            shutil.copy2(src / f, dst / f)
        for o in dst.glob(f + ".orig"):
            o.unlink()
    py = [sys.executable, "-I"]
    subprocess.run(py + [str(HERE / "port_files.py")] +
                   [str(dst / f) for f in ("globalgamemanagers.assets", "resources.assets", "sharedassets0.assets")],
                   check=True)
    subprocess.run(py + [str(HERE / "patch_apis.py"), str(dst / "globalgamemanagers")], check=True)
    for f in SERIALIZED:
        old = set_platform(dst / f, ANDROID)
        print(f"{f}: платформа {old} -> {ANDROID}")
    for o in dst.glob("*.orig"):
        o.unlink()
    print(f"готово: {dst}")


if __name__ == "__main__":
    main()
