#!/usr/bin/env python3
"""pe_exports.py -- имена экспортов Windows-DLL (нативные плагины игры).

    python pe_exports.py file.dll [...]

Нужно, чтобы знать, какие функции нативных плагинов ULTRAKILL на Свитче
придётся заменить заглушками. Только стандартная библиотека.
"""

import struct
import sys


def exports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise SystemExit(f"{path}: не PE")
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    exp_rva = struct.unpack_from("<I", d, dd)[0]
    secs = []
    for i in range(nsec):
        s = opt + optsz + i * 40
        vsize, va, rawsz, raw = struct.unpack_from("<IIII", d, s + 8)
        secs.append((va, max(vsize, rawsz), raw))

    def off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return raw + rva - va
        raise ValueError(hex(rva))

    if not exp_rva:
        return []
    e = off(exp_rva)
    nnames, _, names_rva = struct.unpack_from("<III", d, e + 24)[0], 0, struct.unpack_from("<I", d, e + 32)[0]
    out = []
    for i in range(nnames):
        n = off(struct.unpack_from("<I", d, off(names_rva) + 4 * i)[0])
        out.append(d[n:d.index(b"\0", n)].decode())
    return out


if __name__ == "__main__":
    for p in sys.argv[1:]:
        names = exports(p)
        print(f"== {p} ({len(names)})")
        for n in names:
            print("  " + n)
