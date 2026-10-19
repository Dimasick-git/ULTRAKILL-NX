"""ushader.py -- чтение и сборка блобов сериализованного шейдера Unity 2022.3.

Шейдер хранит программы по платформам: platforms[i] -- номер платформы,
offsets/compressedLengths/decompressedLengths[i][s] -- сегменты s внутри
compressedBlob, каждый сжат LZ4 отдельно (это «чанки» подгрузки вариантов,
по ~16 МБ). Сегмент 0 начинается с таблицы: int n, затем n записей (offset,
length, segment). UnityPy читает только сегмент 0 и разворачивает блоб в
список чисел -- у ULTRAKILL/Master (464 МБ) это валило ПК. Здесь сегменты
распаковываются по требованию, а объект читается через lean_tt.
"""

import struct
import sys

import lz4.block  # noqa: E402  (ставится вместе с UnityPy)

# Платформы ShaderCompilerPlatform и типы программ ShaderGpuProgramType (2022.3)
PLAT_D3D11, PLAT_GLES3, PLAT_GLCORE, PLAT_VULKAN = 4, 9, 15, 18
GPU_GLES31AEP, GPU_GLES31, GPU_GLES3 = 2, 3, 4
GPU_GLCORE32, GPU_GLCORE41, GPU_GLCORE43 = 6, 7, 8


def _as_list(v):
    return list(v) if not isinstance(v, (int,)) else [v]


class PlatformBlobs:
    """Ленивый доступ к блобам одной платформы шейдера."""

    def __init__(self, sh, pidx):
        self.blob = sh["compressedBlob"]            # memoryview из lean_tt
        self.offs = _as_list(sh["offsets"][pidx])
        self.clens = _as_list(sh["compressedLengths"][pidx])
        self.dlens = _as_list(sh["decompressedLengths"][pidx])
        self._cache = {}
        t = self.segment(0)
        n = struct.unpack_from("<i", t, 0)[0]
        self.table = [struct.unpack_from("<iii", t, 4 + k * 12) for k in range(n)]

    def segment(self, s):
        if s not in self._cache:
            if len(self._cache) >= 2:               # держим не больше двух сегментов
                self._cache.pop(next(k for k in self._cache if k != 0))
            o, c, d = self.offs[s], self.clens[s], self.dlens[s]
            self._cache[s] = lz4.block.decompress(bytes(self.blob[o:o + c]), uncompressed_size=d)
        return self._cache[s]

    def __len__(self):
        return len(self.table)

    def get(self, k):
        o, ln, seg = self.table[k]
        return self.segment(seg)[o:o + ln]

    def by_segment(self):
        """Индексы блобов, сгруппированные по сегментам: обход без перераспаковки."""
        groups = {}
        for k, (_, _, seg) in enumerate(self.table):
            groups.setdefault(seg, []).append(k)
        return [groups[s] for s in sorted(groups)]


def pack_platform(blobs, chunk=16 * 1024 * 1024):
    """Собирает блобы платформы обратно в сегменты по ~chunk байт.
    Возвращает список (сжатые байты, несжатая длина); сегмент 0 -- с таблицей."""
    n = len(blobs)
    head = 4 + n * 12
    entries, segs, cur, cur_len = [], [], [], head
    seg_idx = 0
    for b in blobs:
        if cur and cur_len + len(b) > chunk:
            segs.append(cur)
            cur, cur_len, seg_idx = [], 0, seg_idx + 1
        entries.append((cur_len, len(b), seg_idx))
        cur.append(b)
        cur_len += len(b)
    segs.append(cur)
    table = struct.pack("<i", n) + b"".join(struct.pack("<iii", *e) for e in entries)
    out = []
    for i, parts in enumerate(segs):
        raw = (table if i == 0 else b"") + b"".join(parts)
        out.append((lz4.block.compress(raw, mode="high_compression", store_size=False), len(raw)))
    return out


def glsl_text(blob):
    """Текст GLSL внутри блоба GL-программы: строка с префиксом длины.
    Возвращает (текст, смещение текста, длина) или (None, -1, 0)."""
    i = blob.find(b"#ifdef VERTEX")
    if i < 0:
        i = blob.find(b"#ifdef FRAGMENT")
    if i < 4:
        return None, -1, 0
    ln = struct.unpack_from("<I", blob, i - 4)[0]
    return bytes(blob[i:i + ln]).decode("utf8"), i, ln


def replace_glsl(blob, new_text):
    """Блоб с подменённым текстом GLSL; длина и выравнивание строки пересчитаны."""
    text, i, ln = glsl_text(blob)
    end = i + ln
    pad_old = (-end) % 4                    # строка выровнена до 4 байт
    enc = new_text.encode("utf8")
    pad_new = (-(i + len(enc))) % 4
    return bytes(blob[:i - 4]) + struct.pack("<I", len(enc)) + enc + b"\0" * pad_new + bytes(blob[end + pad_old:])
