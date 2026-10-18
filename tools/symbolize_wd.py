#!/usr/bin/env python3
"""symbolize_wd.py -- стеки сторожевого таймера из debug.log в имена функций.

    python symbolize_wd.py debug.log --unity-sym libunity.sym.so --elf ultrakill_nx.elf
        [--il2cpp-sym libil2cpp.sym.so] [--thread UnityMain]

Берёт последний запуск в журнале (после последнего «watchdog armed»), печатает
строки STALL и стек нужного потока, заменяя libunity.so+0x..., libil2cpp.so+0x...
и NRO+0x... на «функция+смещение».
"""

import argparse
import bisect
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import derive_offsets as d  # noqa: E402


class Syms:
    def __init__(self, path):
        s = d.Elf(path).symbols() if path else {}
        items = sorted((v, z, n) for n, (v, z) in s.items() if v)
        self.addr = [i[0] for i in items]
        self.items = items

    def name(self, off):
        i = bisect.bisect_right(self.addr, off) - 1
        if i < 0:
            return "?"
        v, z, n = self.items[i]
        return f"{n[:80]}+0x{off - v:x}"


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--unity-sym")
    ap.add_argument("--il2cpp-sym")
    ap.add_argument("--elf")
    ap.add_argument("--thread", default="UnityMain")
    a = ap.parse_args()
    lines = Path(a.log).read_text(encoding="utf-8", errors="replace").splitlines()
    start = max((i for i, l in enumerate(lines) if "watchdog armed" in l), default=0)
    run = lines[start:]
    mods = {"libunity.so": Syms(a.unity_sym), "libil2cpp.so": Syms(a.il2cpp_sym), "NRO": Syms(a.elf)}
    rx = re.compile(r"(libunity\.so|libil2cpp\.so|NRO)\+0x([0-9a-f]+)")

    def sub(l):
        return rx.sub(lambda m: f"{m.group(0)} [{mods[m.group(1)].name(int(m.group(2), 16))}]", l)

    for l in run:
        if "STALL" in l:
            print(l)
    show = False
    for l in run:
        if l.startswith("[wd]   ") and not l.startswith("[wd]     "):
            show = a.thread in l
        if show and l.startswith("[wd]   "):
            print(sub(l))


if __name__ == "__main__":
    main()
