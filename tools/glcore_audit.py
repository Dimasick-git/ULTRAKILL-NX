#!/usr/bin/env python3
"""glcore_audit.py -- что в GLCore-программах игры не переносится в GLSL ES 3.1.

    python glcore_audit.py <bundle> [<bundle> ...]

Для каждого шейдера берёт блобы платформы 15 (OpenGLCore), ищет GLSL-текст и
считает конструкции, которых нет в GLSL ES 3.1 или которые надо переписать.
"""

import collections
import re
import struct
import sys

sys.path.insert(0, r"C:\Users\dimas\uk-tools\pylib")
import UnityPy  # noqa: E402
from UnityPy.helpers import CompressionHelper  # noqa: E402

PATTERNS = {
    "noperspective": r"\bnoperspective\b",
    "version": r"#version \d+",
    "extension": r"#extension \w+",
    "textureQueryLod": r"\btextureQueryLod\b",
    "textureQueryLevels": r"\btextureQueryLevels\b",
    "gl_ClipDistance": r"\bgl_ClipDistance\b",
    "sample_qualifier": r"^\s*sample\s+(in|out)\b",
    "double": r"\bdouble\b|\bdvec\d\b",
    "imageAtomic": r"\bimageAtomic\w+",
    "sampler1D": r"\bsampler1D\w*\b",
    "samplerBuffer": r"\bsamplerBuffer\b",
    "textureGatherOffsets": r"\btextureGatherOffsets\b",
    "geometry": r"#ifdef GEOMETRY",
    "gl_Layer": r"\bgl_Layer\b",
    "gl_ViewportIndex": r"\bgl_ViewportIndex\b",
    "texture_cube_array": r"\bsamplerCubeArray\b",
    "interpolateAt": r"\binterpolateAt\w+",
}


def platform_blobs(tt, pidx):
    def e(a, k):
        v = a[k]
        return v[0] if isinstance(v, list) else v
    off, cl, dl = e(tt["offsets"], pidx), e(tt["compressedLengths"], pidx), e(tt["decompressedLengths"], pidx)
    data = CompressionHelper.decompress_lz4(bytes(tt["compressedBlob"][off:off + cl]), dl)
    n = struct.unpack_from("<i", data, 0)[0]
    for k in range(n):
        o, ln, _ = struct.unpack_from("<iii", data, 4 + k * 12)
        yield data[o:o + ln]


hits = collections.Counter()
versions = collections.Counter()
exts = collections.Counter()
shaders_with = collections.defaultdict(set)
no_glcore = []
programs = 0

for path in sys.argv[1:]:
    env = UnityPy.load(path)
    for obj in env.objects:
        if obj.type.name not in ("Shader", "ComputeShader"):
            continue
        tt = obj.read_typetree()
        name = tt.get("m_ParsedForm", {}).get("m_Name") or tt.get("m_Name", "?")
        plats = tt.get("platforms") or []
        if obj.type.name == "ComputeShader":
            hits["computeshader_objects"] += 1
            continue
        if 15 not in plats:
            no_glcore.append((name, plats))
            continue
        for blob in platform_blobs(tt, plats.index(15)):
            i = blob.find(b"#ifdef VERTEX")
            if i < 0:
                continue
            programs += 1
            src = blob[i:].decode("utf8", "replace")
            for key, rx in PATTERNS.items():
                for m in re.finditer(rx, src, re.M):
                    if key == "version":
                        versions[m.group(0)] += 1
                    elif key == "extension":
                        exts[m.group(0)] += 1
                    else:
                        hits[key] += 1
                        shaders_with[key].add(name)

print(f"GLSL-программ: {programs}")
print("версии:", dict(versions))
print("расширения:", dict(exts))
print("проблемные конструкции:", dict(hits))
for k, s in shaders_with.items():
    print(f"  {k}: {sorted(s)[:8]}{' ...' if len(s) > 8 else ''}")
print("шейдеры без GLCore:", no_glcore[:20], len(no_glcore))
