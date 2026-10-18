#!/usr/bin/env python3
"""pack_sd.py -- раскладывает нашу Android-сборку ULTRAKILL в папку для SD-карты.

    python pack_sd.py --apk ULTRAKILL.apk --symbols ULTRAKILL.symbols.zip \\
                      [--nro ../loader/ultrakill_nx.nro] -o out

Результат: out/switch/ultrakill/
    ultrakill_nx.nro
    libmain.so libunity.so libil2cpp.so (+ lib_burst_generated.so, если есть)
    assets/          -- папка assets из APK целиком
    offsets.ini      -- tools/derive_offsets.py по символам из symbols.zip

Скопировать out/switch на карту и запускать через подмену тайтла (зажать R
при запуске установленной игры): в режиме альбома памяти не хватит.

Никаких файлов игры в репозитории нет и быть не должно: APK собирается из
твоей копии ULTRAKILL (см. docs/PORTING.md).
"""

import argparse
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIBS = ("libmain.so", "libunity.so", "libil2cpp.so", "lib_burst_generated.so")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apk", required=True)
    ap.add_argument("--symbols", required=True, help="symbols.zip из той же сборки")
    ap.add_argument("--nro", default=str(HERE.parent / "loader" / "ultrakill_nx.nro"))
    ap.add_argument("-o", "--out", default="out")
    a = ap.parse_args()

    dst = Path(a.out) / "switch" / "ultrakill"
    work = Path(a.out) / ".work"
    for p in (dst, work):
        if p.exists():
            shutil.rmtree(p)
        p.mkdir(parents=True)

    with zipfile.ZipFile(a.apk) as z:
        names = z.namelist()
        for lib in LIBS:
            src = f"lib/arm64-v8a/{lib}"
            if src in names:
                (dst / lib).write_bytes(z.read(src))
            elif lib != "lib_burst_generated.so":
                raise SystemExit(f"в APK нет {src}: сборка должна быть ARM64 + IL2CPP")
        n = 0
        for name in names:
            if name.startswith("assets/") and not name.endswith("/"):
                out = dst / name
                out.parent.mkdir(parents=True, exist_ok=True)
                with z.open(name) as f, open(out, "wb") as g:
                    shutil.copyfileobj(f, g)
                n += 1
        print(f"из APK: {sum((dst / l).exists() for l in LIBS)} библиотек, {n} файлов assets")

    with zipfile.ZipFile(a.symbols) as z:
        sym = next((n for n in z.namelist() if n.endswith("arm64-v8a/libil2cpp.sym.so")
                    or n.endswith("arm64-v8a/libil2cpp.so")), None)
        if not sym:
            raise SystemExit("в symbols.zip нет arm64-v8a/libil2cpp.sym.so (нужен режим Debugging)")
        (work / "libil2cpp.sym.so").write_bytes(z.read(sym))

    r = subprocess.run([sys.executable, str(HERE / "derive_offsets.py"),
                        "--lib", str(dst / "libil2cpp.so"), "--sym", str(work / "libil2cpp.sym.so"),
                        "--unity", str(dst / "libunity.so"), "-o", str(dst / "offsets.ini")])
    if r.returncode not in (0,):
        print("derive_offsets.py сообщил о проблеме -- см. вывод выше")

    nro = Path(a.nro)
    if nro.exists():
        shutil.copy2(nro, dst / nro.name)
    else:
        print(f"нет {nro}: собери загрузчик (make в loader/) и положи .nro рядом сам")

    shutil.rmtree(work)
    print(f"готово: {dst}")
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
