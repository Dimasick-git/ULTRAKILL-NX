#!/usr/bin/env python3
"""glsl_es.py -- перевод GLSL-программ Unity для OpenGL Core в GLSL ES 3.20.

В бандлах ULTRAKILL у каждого шейдера есть программы для OpenGLCore (платформа
15): это вывод HLSLcc с #version 150/330/420. GLES-плеер Unity берёт программы
платформы 9 (GLES3Plus), а текст у них отличается мало: версия, точность,
расширения, которых в ES нет или которые там уже в ядре. Этот модуль делает
такую правку; tools/shader_port.py применяет её ко всем шейдерам бандлов.

Как модуль:  convert(text) -> text
Как утилита: python glsl_es.py in.glsl out.glsl
"""

import re

ES_VERSION = "#version 320 es"

# Расширения, которые в GLSL ES 3.20 либо в ядре, либо не нужны.
DROP_EXTENSIONS = {
    "GL_ARB_explicit_attrib_location",
    "GL_ARB_shading_language_420pack",
    "GL_ARB_shader_bit_encoding",
    "GL_ARB_shader_storage_buffer_object",
    "GL_ARB_shader_image_load_store",
    "GL_ARB_shader_image_size",
    "GL_ARB_texture_query_levels",
    "GL_ARB_shader_atomic_counters",
    "GL_ARB_texture_cube_map_array",
    "GL_ARB_gpu_shader5",
    "GL_ARB_texture_gather",
    "GL_ARB_compute_shader",
    "GL_ARB_explicit_uniform_location",
    "GL_ARB_shader_draw_parameters",
}

# Сразу после #version: расширения (обязаны идти до любого кода), затем точность.
PROLOGUE_EXT = """#ifdef GL_EXT_clip_cull_distance
#extension GL_EXT_clip_cull_distance : enable
#endif
"""

PROLOGUE = """precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;
precision highp samplerCube;
precision highp sampler2DArray;
precision highp sampler2DShadow;
precision highp samplerCubeShadow;
precision highp sampler2DArrayShadow;
precision highp samplerCubeArray;
precision highp isampler2D;
precision highp usampler2D;
precision highp isampler3D;
precision highp usampler3D;
precision highp image2D;
precision highp image3D;
precision highp iimage2D;
precision highp uimage2D;
#ifdef GL_EXT_clip_cull_distance
#define NX_CLIPDIST gl_ClipDistance
#else
float nx_clipdist_unused[8];
#define NX_CLIPDIST nx_clipdist_unused
#endif
"""

# textureQueryLevels в ES нет. Unity зовёт его для выбора мипа отражений; у
# текстур игры полная цепочка мипов, поэтому число уровней = log2(размер) + 1.
TQL_HELPERS = """int nx_tql(sampler2D s) { ivec2 z = textureSize(s, 0); return int(log2(float(max(max(z.x, z.y), 1)))) + 1; }
int nx_tql(samplerCube s) { ivec2 z = textureSize(s, 0); return int(log2(float(max(max(z.x, z.y), 1)))) + 1; }
int nx_tql(sampler2DArray s) { ivec3 z = textureSize(s, 0); return int(log2(float(max(max(z.x, z.y), 1)))) + 1; }
int nx_tql(sampler3D s) { ivec3 z = textureSize(s, 0); return int(log2(float(max(max(max(z.x, z.y), z.z), 1)))) + 1; }
int nx_tql(samplerCubeArray s) { ivec3 z = textureSize(s, 0); return int(log2(float(max(max(z.x, z.y), 1)))) + 1; }
"""

RX_VERSION = re.compile(r"^[ \t]*#version[ \t]+\d+([ \t]+\w+)?[ \t]*$", re.M)
RX_EXT = re.compile(r"^[ \t]*#extension[ \t]+(\w+)[ \t]*:[ \t]*\w+[ \t]*$", re.M)
RX_CLIP_REDECL = re.compile(r"^[ \t]*(out|in)[ \t]+float[ \t]+gl_ClipDistance[ \t]*\[[^\]]*\][ \t]*;[ \t]*$", re.M)


def _strip_dead_ifdef(text, ext):
    """Убирает пустые обёртки вида '#ifdef GL_ARB_x\\n#endif' после удаления #extension."""
    return re.sub(r"^[ \t]*#ifdef[ \t]+" + ext + r"[ \t]*\n[ \t]*#endif[ \t]*\n", "", text, flags=re.M)


def convert(text):
    uses_tql = "textureQueryLevels" in text

    def ext_line(m):
        return "" if m.group(1) in DROP_EXTENSIONS else m.group(0)

    text = RX_EXT.sub(ext_line, text)
    for ext in DROP_EXTENSIONS:
        text = _strip_dead_ifdef(text, ext)
    text = RX_CLIP_REDECL.sub("", text)
    text = re.sub(r"\bgl_ClipDistance\b", "NX_CLIPDIST", text)
    if uses_tql:
        text = re.sub(r"\btextureQueryLevels\b", "nx_tql", text)

    # Хелперы textureQueryLevels ссылаются на сэмплеры, поэтому идут после
    # объявлений точности; объявленные, но не вызванные функции GLSL не мешают.
    body = PROLOGUE + (TQL_HELPERS if uses_tql else "")
    return RX_VERSION.sub(lambda m: ES_VERSION + "\n" + PROLOGUE_EXT + body.rstrip("\n"), text)


def stage_source(text, stage):
    """Источник для одной стадии так, как его собирает Unity из общего текста:
    #version первой строкой, затем #define стадии, остальное как есть."""
    versions = RX_VERSION.findall(text)
    m = RX_VERSION.search(text)
    first = m.group(0).strip() if m else ES_VERSION
    body = RX_VERSION.sub("", text)
    # PROLOGUE_EXT вставлен после каждой #version; в собранном тексте он должен
    # стоять сразу за единственной #version, до любого кода.
    body = body.replace(PROLOGUE_EXT, "")
    return f"{first}\n{PROLOGUE_EXT}#define {stage} 1\n{body}", len(versions)


if __name__ == "__main__":
    import sys
    src = open(sys.argv[1], encoding="utf8").read()
    open(sys.argv[2], "w", encoding="utf8", newline="\n").write(convert(src))
