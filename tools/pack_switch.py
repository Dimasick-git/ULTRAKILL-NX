#!/usr/bin/env python3
"""pack_switch.py -- папка для SD-карты Свитча из всех частей порта.

    python pack_switch.py --export Build/AndroidExport --data android-data \\
        --aa aa-android --streaming <StreamingAssets игры> --unity-ini unity29.ini \\
        [--nro loader/ultrakill_nx.nro] -o out

Результат out/switch/ultrakill/:
    ultrakill_nx.nro
    libmain.so libunity.so libil2cpp.so     -- из экспорта Android-сборки (2022.3.29f1, IL2CPP)
    assets/bin/Data/...                      -- экспорт, основные данные -- из игры (prep_data.py)
    assets/aa/...                            -- бандлы с GLES3-шейдерами (port_all.py --target android)
    offsets.ini                              -- unity_syms.py + derive_offsets.py (libil2cpp из symbols)
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MAIN_DATA = ["globalgamemanagers", "globalgamemanagers.assets", "globalgamemanagers.assets.resS",
             "level0", "level0.resS", "resources.assets", "resources.assets.resS",
             "sharedassets0.assets", "sharedassets0.assets.resS"]


def find_one(root, name):
    hits = sorted(Path(root).rglob(name), key=lambda p: len(str(p)))
    return hits[0] if hits else None


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("--export", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--aa", required=True)
    ap.add_argument("--streaming")
    ap.add_argument("--unity-ini", required=True)
    ap.add_argument("--nro", default=str(HERE.parent / "loader" / "ultrakill_nx.nro"))
    ap.add_argument("-o", "--out", default="out")
    a = ap.parse_args()

    exp = Path(a.export)
    main_src = exp / "unityLibrary" / "src" / "main"
    dst = Path(a.out) / "switch" / "ultrakill"
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)

    # 1. библиотеки движка
    libs = main_src / "jniLibs" / "arm64-v8a"
    for lib in ("libmain.so", "libunity.so", "libil2cpp.so"):
        src = libs / lib
        if not src.exists():
            raise SystemExit(f"нет {src}")
        shutil.copy2(src, dst / lib)

    # 2. assets экспорта, затем основные данные игры поверх
    shutil.copytree(main_src / "assets", dst / "assets")
    data = dst / "assets" / "bin" / "Data"
    packed = data / "data.unity3d"
    if packed.exists():
        print("ВНИМАНИЕ: экспорт упаковал данные в data.unity3d -- он содержит сцену из редактора; удаляю")
        packed.unlink()
    # куски *.splitN экспорта -- это данные сцены из редактора; загрузчик склеил
    # бы их поверх данных игры
    for sp in data.glob("*.split*"):
        sp.unlink()
    for f in MAIN_DATA:
        s = Path(a.data) / f
        if s.exists():
            shutil.copy2(s, data / f)
    print(f"основные данные игры: {sum((data / f).exists() for f in MAIN_DATA)} файлов")

    # 3. бандлы и остальное содержимое StreamingAssets
    aa = dst / "assets" / "aa"
    if aa.exists():
        shutil.rmtree(aa)
    shutil.copytree(a.aa, aa)
    subprocess.run([sys.executable, "-I", str(HERE / "fix_catalog.py"), str(aa / "catalog.json")], check=True)
    if a.streaming:
        for extra in ("Videos", "UnityServicesProjectConfiguration.json"):
            s = Path(a.streaming) / extra
            if s.is_dir():
                shutil.copytree(s, dst / "assets" / extra, dirs_exist_ok=True)
            elif s.exists():
                shutil.copy2(s, dst / "assets" / extra)

    # 4. offsets.ini: libunity (символы редактора) + libil2cpp (символы сборки)
    lines = [l for l in Path(a.unity_ini).read_text(encoding="utf-8").splitlines() if l.strip()]
    # .sym.so -- только таблица символов (67 МБ); .dbg.so с отладкой -- 700 МБ
    sym = (find_one(libs, "libil2cpp.sym.so") or find_one(libs, "libil2cpp.dbg.so")
           or find_one(exp / "unityLibrary" / "symbols", "libil2cpp.so"))
    if sym:
        tmp = Path(a.out) / "il2cpp.ini"
        r = subprocess.run([sys.executable, "-I", str(HERE / "derive_offsets.py"), "--lib", str(dst / "libil2cpp.so"),
                            "--sym", str(sym), "-o", str(tmp)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        print(r.stdout.strip())
        if tmp.exists():
            lines += [l for l in tmp.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    else:
        print("ВНИМАНИЕ: символы libil2cpp не найдены -- смещения il2cpp не выведены")
    (dst / "offsets.ini").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # 5. загрузчик
    nro = Path(a.nro)
    if nro.exists():
        shutil.copy2(nro, dst / nro.name)
    total = sum(p.stat().st_size for p in dst.rglob("*") if p.is_file())
    print(f"готово: {dst} ({total / 1e9:.2f} ГБ)")


if __name__ == "__main__":
    main()
