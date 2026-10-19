#!/usr/bin/env python3
"""shader_port.py -- шейдеры бандла: программы OpenGLCore -> GLES3, остальное долой.

    python shader_port.py in.bundle out.bundle [--target android|keep]

Каждый Shader в бандле получает одну платформу -- 9 (GLES3Plus), собранную из
его GLCore-программ (платформа 15):
  * текст GLSL переводится в GLSL ES 3.20 (glsl_es.convert), тип программы в
    заголовке блоба 6/7/8 -> 4 (kShaderGpuProgramGLES3);
  * блобы параметров остаются как есть (GL-бэкенд Unity общий для GLCore и
    GLES, полный список параметров -- надмножество урезанного ES-варианта);
  * подпрограммы плеера раскладываются по уровням 0-2, как в настоящей
    Android-сборке (группа 3 пустая); D3D11 и Vulkan выкидываются -- иначе
    ULTRAKILL/Master занял бы на Свитче 464 МБ памяти вместо ~130.
Раскладка сверена со сборкой тестового шейдера под Android (compare_probe.py).

--target android дополнительно помечает сериализованные файлы бандла как
собранные под Android (BuildTarget 13); keep -- оставить как есть (для проверки
в Windows-плеере с GLES3).

Объекты читаются через lean_tt, сегменты пишутся потоком: 1.8 ГБ текста
Master никогда не лежат в памяти целиком.
"""

import argparse
import array
import gc
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import lz4.block  # noqa: E402
import glsl_es  # noqa: E402
import lean_tt  # noqa: E402
import ushader  # noqa: E402

GL_TYPES = (ushader.GPU_GLCORE32, ushader.GPU_GLCORE41, ushader.GPU_GLCORE43)
CHUNK = 16 * 1024 * 1024
PROGS = ("progVertex", "progFragment", "progGeometry", "progHull", "progDomain", "progRayTracing")


def _like(old, values):
    """Новый массив того же вида, что и старый (array.array / memoryview / list)."""
    if isinstance(old, array.array):
        return array.array(old.typecode, values)
    if isinstance(old, (memoryview, bytes, bytearray)):
        return bytes(values)
    return list(values)


def convert_blob(blob, stats):
    text, i, ln = ushader.glsl_text(blob)
    if text is None:
        return bytes(blob)                       # блоб параметров
    out = ushader.replace_glsl(blob, glsl_es.convert(text))
    t = struct.unpack_from("<i", out, 4)[0]
    if t in GL_TYPES:
        out = out[:4] + struct.pack("<i", ushader.GPU_GLES3) + out[8:]
    else:
        stats["odd_type"] += 1
    stats["programs"] += 1
    return out


def rebuild_platform(sh, gi, stats):
    """GLCore-блобы -> сжатые сегменты GLES; сегмент 0 -- только таблица."""
    pb = ushader.PlatformBlobs(sh, gi)
    n = len(pb)
    entries = [None] * n
    segs = []                                    # (сжатые, длина) для сегментов 1..N
    cur, cur_len = [], 0

    def flush():
        nonlocal cur, cur_len
        if cur:
            raw = b"".join(cur)
            segs.append((lz4.block.compress(raw, mode="high_compression", store_size=False), len(raw)))
            cur, cur_len = [], 0

    for group in pb.by_segment():
        for k in group:
            b = convert_blob(pb.get(k), stats)
            if cur and cur_len + len(b) > CHUNK:
                flush()
            entries[k] = (cur_len, len(b), len(segs) + 1)
            cur.append(b)
            cur_len += len(b)
    flush()
    table = struct.pack("<i", n) + b"".join(struct.pack("<iii", *e) for e in entries)
    seg0 = (lz4.block.compress(table, mode="high_compression", store_size=False), len(table))
    allsegs = [seg0] + segs
    blob, offs, clens, dlens = bytearray(), [], [], []
    for c, d in allsegs:
        offs.append(len(blob))
        clens.append(len(c))
        dlens.append(d)
        blob += c
    return bytes(blob), offs, clens, dlens


def port_program(prog):
    psp = prog.get("m_PlayerSubPrograms")
    if psp is None:
        return
    pbi = prog.get("m_ParameterBlobIndices") or []
    gl, gl_params = [], []
    for g, group in enumerate(psp):
        params = list(pbi[g]) if g < len(pbi) else []
        for j, sp in enumerate(group):
            if sp["m_GpuProgramType"] in GL_TYPES:
                e = dict(sp)
                e["m_GpuProgramType"] = ushader.GPU_GLES3
                gl.append(e)
                if j < len(params):
                    gl_params.append(params[j])
    if not gl:
        prog["m_PlayerSubPrograms"] = []
        prog["m_ParameterBlobIndices"] = []
        return
    prog["m_PlayerSubPrograms"] = [list(gl), list(gl), list(gl), []]
    tmpl = pbi[0] if pbi else array.array("I")
    if gl_params:
        prog["m_ParameterBlobIndices"] = [_like(tmpl, gl_params) for _ in range(3)] + [_like(tmpl, [])]
    else:
        prog["m_ParameterBlobIndices"] = [_like(tmpl, []) for _ in range(4)]


def convert_dict(sh, stats):
    """Значение шейдера из lean_tt.read -> только GLES3 (меняется на месте).
    Возвращает (имя, пометка, изменён ли)."""
    name = sh["m_ParsedForm"]["m_Name"]
    plats = list(sh["platforms"])
    if plats == [ushader.PLAT_GLES3]:
        return name, "уже GLES3", False
    if ushader.PLAT_GLCORE not in plats:
        return name, f"нет GLCore (платформы {plats}) -- оставлен как есть", False
    gi = plats.index(ushader.PLAT_GLCORE)

    blob, offs, clens, dlens = rebuild_platform(sh, gi, stats)

    sh["platforms"] = _like(sh["platforms"], [ushader.PLAT_GLES3])
    sh["offsets"] = [_like(sh["offsets"][gi], offs)]
    sh["compressedLengths"] = [_like(sh["compressedLengths"][gi], clens)]
    sh["decompressedLengths"] = [_like(sh["decompressedLengths"][gi], dlens)]
    sh["compressedBlob"] = blob
    sh["stageCounts"] = _like(sh["stageCounts"], [sh["stageCounts"][gi]])

    for sub in sh["m_ParsedForm"]["m_SubShaders"]:
        for p in sub["m_Passes"]:
            if "m_Platforms" in p:
                p["m_Platforms"] = _like(p["m_Platforms"], [ushader.PLAT_GLES3])
            if "m_EditorDataHash" in p and len(p["m_EditorDataHash"]) == len(plats):
                p["m_EditorDataHash"] = [p["m_EditorDataHash"][gi]]
            for key in PROGS:
                if key in p:
                    port_program(p[key])
    return name, "", True


def port_shader(obj, stats):
    sh, spans, data = lean_tt.read(obj)
    name, note, changed = convert_dict(sh, stats)
    if not changed:
        return name, note
    new = lean_tt.write(obj, sh)
    obj.set_raw_data(new)
    return name, f"{len(data) / 1e6:.1f} -> {len(new) / 1e6:.1f} МБ"


def mark_android(env):
    """BuildTarget сериализованных файлов бандла -> 13 (Android)."""
    n = 0
    for f in env.files.values():
        for sf in getattr(f, "files", {}).values():
            if hasattr(sf, "_m_target_platform"):   # именно это поле пишется в файл
                sf._m_target_platform = 13
                n += 1
    return n


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--target", choices=("android", "keep"), default="keep")
    a = ap.parse_args()
    import UnityPy

    t0 = time.time()
    env = UnityPy.load(a.src)
    stats = {"programs": 0, "odd_type": 0, "shaders": 0}
    for obj in env.objects:
        if obj.type.name != "Shader":
            continue
        name, note = port_shader(obj, stats)
        stats["shaders"] += 1
        print(f"  {name}: {note}", flush=True)
        gc.collect()
    if a.target == "android":
        print(f"  помечено как Android: {mark_android(env)} файлов")
    data = env.file.save(packer="lz4")
    Path(a.dst).write_bytes(data)
    print(f"{a.dst}: шейдеров {stats['shaders']}, программ {stats['programs']} "
          f"(странный тип: {stats['odd_type']}), {len(data) / 1e6:.1f} МБ, {time.time() - t0:.0f} с")


if __name__ == "__main__":
    main()
