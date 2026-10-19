#!/usr/bin/env python3
"""compare_probe.py -- один и тот же шейдер в бандле Windows (GLCore) и Android
(GLES3): платформы, типы подпрограмм, устройство блобов программ и параметров.

    python compare_probe.py <windows bundle> <android bundle> [outdir]
"""

import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import UnityPy  # noqa: E402
import lean_tt  # noqa: E402
import ushader  # noqa: E402

out = Path(sys.argv[3]) if len(sys.argv) > 3 else None


def load(path):
    env = UnityPy.load(path)
    for obj in env.objects:
        if obj.type.name == "Shader":
            sh, spans, data = lean_tt.read(obj)
            return sh
    raise SystemExit("нет шейдера в " + path)


def describe(tag, sh):
    pf = sh["m_ParsedForm"]
    plats = list(sh["platforms"])
    print(f"== {tag}: platforms {plats}, keywords {pf['m_KeywordNames']}")
    p = pf["m_SubShaders"][0]["m_Passes"][0]
    print("   pass m_Platforms", list(p["m_Platforms"]) if "m_Platforms" in p else "-")
    for st in ("progVertex", "progFragment"):
        prog = p[st]
        for gi, g in enumerate(prog["m_PlayerSubPrograms"]):
            for sp in g:
                print(f"   {st} g{gi}: blob {sp['m_BlobIndex']} type {sp['m_GpuProgramType']} "
                      f"kw {list(sp['m_KeywordIndices'])} req {sp.get('m_ShaderRequirements')}")
        print(f"   {st} paramBlobIdx {[list(x) for x in prog['m_ParameterBlobIndices']]}")
        print(f"   {st} mask {prog.get('m_SerializedKeywordStateMask')}")
    blobs = {}
    for i, pl in enumerate(plats):
        pb = ushader.PlatformBlobs(sh, i)
        blobs[pl] = [bytes(pb.get(k)) for k in range(len(pb))]
        print(f"   platform {pl}: {len(pb)} blobs, sizes {[len(b) for b in blobs[pl]]}")
        if out:
            for k, b in enumerate(blobs[pl]):
                (out / f"{tag}_p{pl}_b{k}.bin").write_bytes(b)
    return blobs


if out:
    out.mkdir(parents=True, exist_ok=True)
win = describe("win", load(sys.argv[1]))
andr = describe("android", load(sys.argv[2]))

gl = [b for b in win[15] if ushader.glsl_text(b)[0]]
es = [b for b in andr[9] if ushader.glsl_text(b)[0]]
for tag, b in (("GLCore", gl[0]), ("GLES3", es[0])):
    t, i, ln = ushader.glsl_text(b)
    print(f"== {tag} program blob: len {len(b)}, text at {i} len {ln}")
    print("   head:", b[:i].hex())
    print("   tail after text:", b[i + ln:i + ln + 96].hex(), "... total tail", len(b) - i - ln)
    print("   text start:", t[:400].replace("\n", " | "))
