#!/usr/bin/env python3
"""mem_probe.py -- сколько памяти съедает чтение шейдеров UnityPy (ПК падал от нехватки).

    python mem_probe.py <bundle> [имя шейдера ...]

Печатает тип compressedBlob и рост памяти процесса на каждый шейдер; самые
крупные объекты не трогает, если их сырой размер больше --max-mb.
"""

import argparse
import ctypes
import ctypes.wintypes
import gc
import os
import sys

sys.path.insert(0, os.environ.get("UNITYPY_PATH", os.path.expanduser("~/uk-tools/pylib")))
import UnityPy  # noqa: E402


class PMC(ctypes.Structure):
    _fields_ = [("cb", ctypes.wintypes.DWORD), ("PageFaultCount", ctypes.wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]


def priv_mb():
    c = PMC()
    c.cb = ctypes.sizeof(PMC)
    ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.windll.kernel32.GetCurrentProcess(), ctypes.byref(c), c.cb)
    return c.PagefileUsage / 1048576


ap = argparse.ArgumentParser()
ap.add_argument("bundle")
ap.add_argument("names", nargs="*")
ap.add_argument("--max-mb", type=float, default=4.0)
a = ap.parse_args()

print(f"старт: {priv_mb():.0f} МБ")
env = UnityPy.load(a.bundle)
print(f"после load: {priv_mb():.0f} МБ")
sizes = []
for obj in env.objects:
    if obj.type.name == "Shader":
        sizes.append((obj.byte_size, obj))
sizes.sort(key=lambda x: x[0])
print(f"шейдеров {len(sizes)}, сырые размеры МБ: мин {sizes[0][0]/1e6:.2f} медиана {sizes[len(sizes)//2][0]/1e6:.2f} "
      f"макс {sizes[-1][0]/1e6:.2f}; сумма {sum(s for s, _ in sizes)/1e6:.0f}")
print("крупнейшие:", [(round(s / 1e6, 1)) for s, _ in sizes[-8:]])
print(f"после перечисления: {priv_mb():.0f} МБ")
for s, obj in sizes:
    if s / 1e6 > a.max_mb:
        continue
    before = priv_mb()
    tt = obj.read_typetree()
    name = tt["m_ParsedForm"]["m_Name"]
    if a.names and name not in a.names:
        del tt
        continue
    cb = tt["compressedBlob"]
    print(f"{name}: raw {s/1e6:.2f} МБ, compressedBlob {type(cb).__name__} len {len(cb)}, "
          f"память +{priv_mb() - before:.0f} МБ")
    del tt, cb
    gc.collect()
    if not a.names:
        break
print(f"конец: {priv_mb():.0f} МБ")
