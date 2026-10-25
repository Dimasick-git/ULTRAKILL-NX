#!/usr/bin/env python3
"""validate_es.py -- переводит GLCore-программы шейдеров в GLSL ES 3.20 и
проверяет каждую стадию компилятором glslang (Khronos), без консоли.

    python validate_es.py --glslang glslang.exe -w workdir [--sample N] <bundle> [...]

У каждого шейдера проверяется не больше N программ (равномерная выборка по
всем сегментам; у ULTRAKILL/Master их 59 904). Объекты читаются через lean_tt,
сегменты распаковываются по одному -- без этого ПК на 7.7 ГБ выключался.
Тексты непрошедших стадий остаются в workdir/fail.
"""

import argparse
import collections
import gc
import re
import shutil
import subprocess
import os
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, os.environ.get("UNITYPY_PATH", os.path.expanduser("~/uk-tools/pylib")))
import UnityPy  # noqa: E402
import glsl_es  # noqa: E402
import lean_tt  # noqa: E402
import ushader  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--glslang", required=True)
ap.add_argument("-w", "--work", default="es-validate")
ap.add_argument("--sample", type=int, default=400)
ap.add_argument("bundles", nargs="+")
a = ap.parse_args()

work = Path(a.work)
if work.exists():
    shutil.rmtree(work)
(work / "src").mkdir(parents=True)
(work / "fail").mkdir()

fails = collections.defaultdict(list)
ok = total = 0
seen = set()


def run_glslang(jobs):
    global ok
    r = subprocess.run([a.glslang, *[str(j[0]) for j in jobs]], capture_output=True, text=True,
                       encoding="utf8", errors="replace")
    blocks = re.split(r"^(?=\S.*\.(?:vert|frag)\s*$)", r.stdout + r.stderr, flags=re.M)
    bad = {}
    for b in blocks:
        lines = b.strip().splitlines()
        if lines:
            errs = [l for l in lines[1:] if l.startswith("ERROR")]
            if errs:
                bad[Path(lines[0].strip()).name] = errs
    for f, name, bi, stage in jobs:
        if f.name in bad:
            fails[name].append((bi, stage, bad[f.name][:3]))
            shutil.copy2(f, work / "fail" / f.name)
        else:
            ok += 1
        f.unlink()


for path in a.bundles:
    env = UnityPy.load(path)
    for obj in env.objects:
        if obj.type.name != "Shader":
            continue
        sh, spans, data = lean_tt.read(obj, max_depth_spans=1)
        name = sh["m_ParsedForm"]["m_Name"]
        plats = list(sh["platforms"])
        if name in seen or ushader.PLAT_GLCORE not in plats:
            continue
        seen.add(name)
        pb = ushader.PlatformBlobs(sh, plats.index(ushader.PLAT_GLCORE))
        n = len(pb)
        step = max(1, n // a.sample)
        picks = set(range(0, n, step))
        jobs = []
        safe = re.sub(r"[^A-Za-z0-9]+", "_", name)
        for group in pb.by_segment():
            for bi in group:
                if bi not in picks:
                    continue
                src, _, _ = ushader.glsl_text(pb.get(bi))
                if src is None:
                    continue
                text = glsl_es.convert(src)
                for stage, ext in (("VERTEX", "vert"), ("FRAGMENT", "frag")):
                    if f"#ifdef {stage}" not in text:
                        continue
                    s, _ = glsl_es.stage_source(text, stage)
                    f = work / "src" / f"{safe}__{bi}.{ext}"
                    f.write_text(s, encoding="utf8", newline="\n")
                    jobs.append((f, name, bi, stage))
                    total += 1
                if len(jobs) >= 200:
                    run_glslang(jobs)
                    jobs = []
        if jobs:
            run_glslang(jobs)
        print(f"{name}: программ {n}, проверено {len(picks)}", flush=True)
        del sh, spans, data, pb
        gc.collect()
    del env
    gc.collect()

print(f"\nпрошло {ok}/{total} стадий; шейдеров с ошибками: {len(fails)}")
errkinds = collections.Counter()
for name, items in fails.items():
    for bi, stage, errs in items:
        for e in errs:
            errkinds[re.sub(r"\d+:\d+:", "", e)[:110]] += 1
for k, v in errkinds.most_common(25):
    print(f"  {v:5d}  {k}")
for name, items in list(fails.items())[:15]:
    bi, stage, errs = items[0]
    print(f"- {name} [{stage} blob {bi}] x{len(items)}: {errs[0][:150]}")
