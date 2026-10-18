#!/usr/bin/env python3
"""port_all.py -- все бандлы Addressables игры -> дерево с GLES3-шейдерами.

    python port_all.py <ULTRAKILL_Data/StreamingAssets/aa> <выход aa> [--target android|keep]

Бандл с шейдерами прогоняется через shader_port (каждый в отдельном процессе:
память освобождается гарантированно, а падение одного не рушит остальные), без
шейдеров -- копируется как есть. catalog.json, settings.json и AddressablesLink
копируются. Повторный запуск пропускает уже готовые бандлы.
"""

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, r"C:\Users\dimas\uk-tools\pylib")


def has_shaders(path):
    import UnityPy
    env = UnityPy.load(str(path))
    found = any(o.type.name == "Shader" for o in env.objects)
    del env
    return found


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--target", choices=("android", "keep"), default="keep")
    a = ap.parse_args()
    src, dst = Path(a.src), Path(a.dst)
    dst.mkdir(parents=True, exist_ok=True)
    for name in ("catalog.json", "settings.json"):
        shutil.copy2(src / name, dst / name)
    if (src / "AddressablesLink").exists():
        shutil.copytree(src / "AddressablesLink", dst / "AddressablesLink", dirs_exist_ok=True)

    bundles = sorted(src.rglob("*.bundle"), key=lambda p: p.stat().st_size)
    t0 = time.time()
    ported = copied = failed = 0
    for i, b in enumerate(bundles):
        out = dst / b.relative_to(src)
        out.parent.mkdir(parents=True, exist_ok=True)
        if out.exists() and out.stat().st_size > 0:
            continue
        # дешёвая проверка в дочернем процессе: память родителя не растёт
        r = subprocess.run([sys.executable, "-I", "-c",
                            "import sys; sys.path.insert(0, r'%s'); sys.path.insert(0, r'%s');"
                            "import port_all; print(int(port_all.has_shaders(sys.argv[1])))" % (HERE, r"C:\Users\dimas\uk-tools\pylib"),
                            str(b)], capture_output=True, text=True)
        need = r.stdout.strip().endswith("1")
        if need or a.target == "android":
            r = subprocess.run([sys.executable, "-I", str(HERE / "shader_port.py"), str(b), str(out),
                                "--target", a.target], capture_output=True, text=True,
                               encoding="utf8", errors="replace")
            if r.returncode == 0:
                ported += 1
                last = (r.stdout.strip().splitlines() or ["?"])[-1]
                print(f"[{i + 1}/{len(bundles)}] {b.name}: {last}", flush=True)
            else:
                failed += 1
                print(f"[{i + 1}/{len(bundles)}] {b.name}: ОШИБКА\n{r.stderr[-800:]}", flush=True)
                if out.exists():
                    out.unlink()
        else:
            shutil.copy2(b, out)
            copied += 1
    print(f"готово за {time.time() - t0:.0f} с: переделано {ported}, скопировано {copied}, ошибок {failed}")


if __name__ == "__main__":
    main()
