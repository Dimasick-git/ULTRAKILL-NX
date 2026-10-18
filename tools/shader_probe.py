#!/usr/bin/env python3
"""shader_probe.py -- устройство сериализованного шейдера Unity 2022.3 в бандле.

    python shader_probe.py <bundle> <имя шейдера> [outdir]

Печатает платформы, проходы, подпрограммы плеера (m_PlayerSubPrograms) с
индексами блобов и типами, и выкладывает DXBC первой пары VS/PS в outdir.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, r"C:\Users\dimas\uk-tools\pylib")
import UnityPy  # noqa: E402
from UnityPy.helpers import CompressionHelper  # noqa: E402

bundle, want = sys.argv[1], sys.argv[2]
outdir = Path(sys.argv[3]) if len(sys.argv) > 3 else None


def blobs_for_platform(tt, i):
    def entry(arr, k):
        v = arr[k]
        return v[0] if isinstance(v, list) else v
    off = entry(tt["offsets"], i)
    clen = entry(tt["compressedLengths"], i)
    dlen = entry(tt["decompressedLengths"], i)
    raw = bytes(tt["compressedBlob"][off:off + clen])
    data = CompressionHelper.decompress_lz4(raw, dlen)
    n = struct.unpack_from("<i", data, 0)[0]
    out = []
    for k in range(n):
        o, ln, seg = struct.unpack_from("<iii", data, 4 + k * 12)
        out.append(data[o:o + ln])
    return out


env = UnityPy.load(bundle)
for obj in env.objects:
    if obj.type.name != "Shader":
        continue
    tt = obj.read_typetree()
    name = tt["m_ParsedForm"]["m_Name"]
    if name != want:
        continue
    print("shader:", name, "pathid", obj.path_id)
    print("platforms:", tt["platforms"])
    print("top keys:", list(tt.keys()))
    pf = tt["m_ParsedForm"]
    print("parsed keys:", list(pf.keys()))
    print("keywords:", len(pf.get("m_KeywordNames", [])), pf.get("m_KeywordNames", [])[:20])
    blobs = blobs_for_platform(tt, 0)
    print("blobs in platform 0:", len(blobs))
    sub = pf["m_SubShaders"][0]
    p = sub["m_Passes"][0]
    print("pass keys:", list(p.keys()))
    for stage in ("progVertex", "progFragment"):
        prog = p[stage]
        print(stage, "keys:", list(prog.keys()))
        psp = prog.get("m_PlayerSubPrograms") or []
        print("  player subprogram groups:", len(psp), "first group size:", len(psp[0]) if psp else 0)
        if psp and psp[0]:
            print("  first:", {k: v for k, v in psp[0][0].items()})
        pbi = prog.get("m_ParameterBlobIndices") or []
        print("  parameter blob indices groups:", len(pbi), (pbi[0][:4] if pbi else None))
    for stage in ("progVertex", "progFragment"):
        psp = p[stage]["m_PlayerSubPrograms"]
        print(stage, "group sizes:", [len(g) for g in psp])
        for gi, g in enumerate(psp):
            for sp in g[:3]:
                print(f"   g{gi}: blob {sp['m_BlobIndex']} type {sp['m_GpuProgramType']} kw {sp['m_KeywordIndices']}")
    if outdir:
        outdir.mkdir(parents=True, exist_ok=True)
        for pi in range(len(tt["platforms"])):
            bl = blobs_for_platform(tt, pi)
            for bi, b in enumerate(bl):
                (outdir / f"p{tt['platforms'][pi]}_b{bi}.bin").write_bytes(b)
            print(f"  platform {tt['platforms'][pi]}: {len(bl)} blobs -> {outdir}")
    break
