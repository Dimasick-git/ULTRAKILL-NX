#!/usr/bin/env python3
"""derive_offsets.py -- offsets.ini для ultrakill_nx из НАШЕЙ сборки игры.

Загрузчику нужны адреса внутри libil2cpp.so, которые у каждой сборки свои:
глобальные переменные Boehm GC (мост остановки мира), куда il2cpp кладёт
JavaVM*, и обёртки UnityEngine.Time.get_*. В порте Papers, Please их искали
дизассемблером руками. Мы собираем Android-часть сами, поэтому берём их из
символов: Unity кладёт неурезанные библиотеки в symbols.zip (Player Settings ->
Publishing -> Create symbols.zip: Debugging).

Использование:
    python derive_offsets.py --lib libil2cpp.so --sym libil2cpp.sym.so \\
                             [--unity libunity.so] [--main-c ../loader/source/main.c] \\
                             -o offsets.ini

--lib    библиотека из APK (lib/arm64-v8a/libil2cpp.so) -- код читаем из неё
--sym    та же библиотека с символами (из symbols.zip)
--unity  libunity.so из APK: сверить слова на местах заплаток main.c
--main-c откуда брать таблицу заплаток libunity (по умолчанию соседний loader)

Только стандартная библиотека Python, ELF64 little-endian (arm64).
"""

import argparse
import re
import struct
import sys
from pathlib import Path


class Elf:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 2 or d[5] != 1:
            raise SystemExit(f"{path}: не ELF64 little-endian")
        (self.e_phoff, self.e_shoff) = struct.unpack_from("<QQ", d, 0x20)
        (self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum,
         self.e_shstrndx) = struct.unpack_from("<HHHHH", d, 0x36)
        self.sections = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            (name, typ, flags, addr, offset, size, link, info, align,
             entsize) = struct.unpack_from("<IIQQQQIIQQ", d, off)
            self.sections.append(dict(name_off=name, type=typ, addr=addr, offset=offset,
                                      size=size, link=link, entsize=entsize))
        if self.sections and self.e_shstrndx < len(self.sections):
            shstr = self.sections[self.e_shstrndx]
            for s in self.sections:
                s["name"] = self._cstr(shstr["offset"] + s["name_off"])
        self.segments = []
        for i in range(self.e_phnum):
            off = self.e_phoff + i * self.e_phentsize
            (typ, flags, offset, vaddr, paddr, filesz, memsz,
             align) = struct.unpack_from("<IIQQQQQQ", d, off)
            self.segments.append(dict(type=typ, offset=offset, vaddr=vaddr, filesz=filesz))

    def _cstr(self, off):
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("utf-8", "replace")

    def section(self, name):
        for s in self.sections:
            if s.get("name") == name:
                return s
        return None

    def symbols(self):
        """имя -> (значение, размер); .symtab, а если его нет -- .dynsym."""
        out = {}
        for tab, strs in ((".dynsym", ".dynstr"), (".symtab", ".strtab")):
            st, ss = self.section(tab), self.section(strs)
            if not st or not ss or st["type"] == 8:      # SHT_NOBITS
                continue
            for i in range(st["size"] // 24):
                off = st["offset"] + i * 24
                name, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", self.data, off)
                if not name or not value:
                    continue
                out[self._cstr(ss["offset"] + name)] = (value, size)
        return out

    def build_id(self):
        for s in self.sections:
            if s.get("name") == ".note.gnu.build-id":
                namesz, descsz, typ = struct.unpack_from("<III", self.data, s["offset"])
                desc = s["offset"] + 12 + ((namesz + 3) & ~3)
                return self.data[desc:desc + descsz].hex()
        return None

    def read_va(self, va, n):
        for seg in self.segments:
            if seg["type"] == 1 and seg["vaddr"] <= va < seg["vaddr"] + seg["filesz"]:
                off = seg["offset"] + (va - seg["vaddr"])
                return self.data[off:off + n]
        raise ValueError(f"адрес 0x{va:x} вне загружаемых сегментов {self.path.name}")


# --- немного AArch64: ровно то, что нужно для JNI_OnLoad ---------------------

def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def find_javavm_store(elf, onload_va):
    """JNI_OnLoad il2cpp: mov xN, x0 ... adrp xM, page ... str xN, [xM, #imm].
    Возвращает адрес глобала, в который сохраняется JavaVM*."""
    code = elf.read_va(onload_va, 4 * 48)
    vm_regs = {0}                 # регистры, в которых сейчас лежит JavaVM*
    pages = {}                    # регистр -> значение после adrp
    for i in range(0, len(code), 4):
        ins = struct.unpack_from("<I", code, i)[0]
        pc = onload_va + i
        if (ins & 0x7FE0FFE0) == 0x2A0003E0 and (ins >> 31):        # mov xd, xm (orr)
            rd, rm = ins & 31, (ins >> 16) & 31
            if rm in vm_regs:
                vm_regs.add(rd)
            else:
                vm_regs.discard(rd)
            continue
        if (ins & 0x9F000000) == 0x90000000:                          # adrp
            rd = ins & 31
            imm = ((ins >> 29) & 3) | (((ins >> 5) & 0x7FFFF) << 2)
            pages[rd] = (pc & ~0xFFF) + (sx(imm, 21) << 12)
            continue
        if (ins & 0xFFC00000) == 0xF9000000:                          # str xt, [xn, #imm]
            rt, rn = ins & 31, (ins >> 5) & 31
            imm = ((ins >> 10) & 0xFFF) * 8
            if rt in vm_regs and rn in pages:
                return pages[rn] + imm
        if (ins & 0xFC000000) == 0x94000000 or (ins & 0xFFFFFC1F) == 0xD63F0000:
            vm_regs = {r for r in vm_regs if 19 <= r <= 28}   # bl/blr портят x0-x18
        if ins == 0xD65F03C0:                                         # ret
            break
    return None


# --- что ищем ----------------------------------------------------------------

GC_SYMBOLS = {
    "il2cpp.gc_suspend_sig": ["GC_sig_suspend"],
    "il2cpp.gc_restart_sig": ["GC_sig_thr_restart"],
    "il2cpp.gc_start_ack":   ["GC_retry_signals"],
    "il2cpp.gc_ack_sem":     ["GC_suspend_ack_sem"],
}

TIME_PREFIXES = {
    "il2cpp.time_get_deltaTime":                    "Time_get_deltaTime_m",
    "il2cpp.time_get_frameCount":                   "Time_get_frameCount_m",
    "il2cpp.time_get_realtimeSinceStartupAsDouble": "Time_get_realtimeSinceStartupAsDouble_m",
    "il2cpp.time_get_realtimeSinceStartup":         "Time_get_realtimeSinceStartup_m",
    # ULTRAKILL на старте глушит журнал (filterLogType = Error); загрузчик
    # ставит сюда «return true», чтобы сообщения игры шли в debug.log
    "il2cpp.logger_is_allowed":                     "Logger_IsLogTypeAllowed_m",
    # Bootstrap.Start ставит filterLogType = Error через этот сеттер; загрузчик
    # делает его пустым (IsLogTypeAllowed il2cpp встраивает в вызывающих)
    "il2cpp.logger_set_filter":                     "Logger_set_filterLogType_m",
}

VIDEO_METHODS = (
    'get_isPlaying', 'get_isPaused', 'get_isPrepared', 'get_time', 'get_length',
    'get_frame', 'get_frameRate', 'get_frameCount', 'get_width', 'get_height',
    'get_texture', 'Play', 'Pause', 'Stop', 'Prepare', 'set_time', 'set_frame',
)


def derive_il2cpp(lib, sym):
    syms = sym.symbols()
    if not syms:
        raise SystemExit(f"{sym.path}: символов нет -- нужен файл из symbols.zip (Debugging)")
    out, notes = {}, []

    for key, names in GC_SYMBOLS.items():
        hit = next((syms[n][0] for n in names if n in syms), None)
        if hit:
            out[key] = hit
        else:
            notes.append(f"{key}: нет символа {names[0]} -- мост GC останется выключен")

    for key, prefix in TIME_PREFIXES.items():
        cands = sorted(n for n in syms if n.startswith(prefix) and not n.endswith("_Injected"))
        if len(cands) == 1:
            out[key] = syms[cands[0]][0]
        elif len(cands) > 1:
            notes.append(f"{key}: несколько кандидатов {cands[:3]} -- не трогаю")
        # нет кандидата -- член вырезан стрипингом, это нормально

    for method in VIDEO_METHODS:
        matches = [(n, a, size) for n, (a, size) in syms.items()
                   if n.startswith('VideoPlayer_' + method + '_m')]
        if len(matches) == 1 and matches[0][2] >= 16:
            out['il2cpp.video_' + method] = matches[0][1]
        else:
            notes.append(f'VideoPlayer.{method}: absent/ambiguous or too short for hook')

    onload = lib.symbols().get("JNI_OnLoad")
    if onload:
        vm = find_javavm_store(lib, onload[0])
        if vm:
            out["il2cpp.javavm"] = vm
        else:
            notes.append("il2cpp.javavm: в JNI_OnLoad не нашлось str JavaVM* в глобал")
    else:
        notes.append("il2cpp.javavm: в libil2cpp.so нет экспорта JNI_OnLoad")
    return out, notes


def check_libunity(unity, main_c):
    """Сверяет слова libunity.so на местах заплаток из таблицы в main.c."""
    src = Path(main_c).read_text(encoding="utf-8")
    table = re.findall(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\}", src)
    singles = {
        "NXU_OFF_FMOD_OUTPUT": 0x2A1503E1,
        "NXU_OFF_FMOD_BUFGEOM": 0x54000089,
        "NXU_OFF_SWAPPY_GETTER": 0xA9BF4FFE,
    }
    hdr = (Path(main_c).parent / "nx_patch_unity.h").read_text(encoding="utf-8")
    ok = bad = 0
    for off, frm, to in table:
        off, frm, to = int(off, 16), int(frm, 16), int(to, 16)
        cur = struct.unpack("<I", unity.read_va(off, 4))[0]
        if cur in (frm, to):
            ok += 1
        else:
            bad += 1
            print(f"  ! libunity+0x{off:x}: 0x{cur:08x}, ждали 0x{frm:08x}")
    for name, want in singles.items():
        m = re.search(rf"#define\s+{name}\s+(0x[0-9a-fA-F]+)", hdr)
        if not m:
            continue
        off = int(m.group(1), 16)
        cur = struct.unpack("<I", unity.read_va(off, 4))[0]
        if cur == want:
            ok += 1
        else:
            bad += 1
            print(f"  ! {name} libunity+0x{off:x}: 0x{cur:08x}, ждали 0x{want:08x}")
    return ok, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--sym", required=True)
    ap.add_argument("--unity")
    ap.add_argument("--main-c", default=str(Path(__file__).resolve().parent.parent / "loader/source/main.c"))
    ap.add_argument("-o", "--out", default="offsets.ini")
    a = ap.parse_args()

    lib, sym = Elf(a.lib), Elf(a.sym)
    bl, bs = lib.build_id(), sym.build_id()
    if bl and bs and bl != bs:
        raise SystemExit(f"BuildID не совпадают: {a.lib} {bl} != {a.sym} {bs} -- символы от другой сборки")

    out, notes = derive_il2cpp(lib, sym)

    lines = ["# offsets.ini -- сгенерировано tools/derive_offsets.py, руками не править",
             f"# libil2cpp.so build-id {bl or '?'}"]
    lines += [f"{k} = 0x{v:x}" for k, v in sorted(out.items())]
    Path(a.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{a.out}: {len(out)} смещений")
    for k, v in sorted(out.items()):
        print(f"  {k} = 0x{v:x}")
    for n in notes:
        print(f"  - {n}")

    if a.unity:
        ok, bad = check_libunity(Elf(a.unity), a.main_c)
        print(f"libunity: заплаток совпало {ok}, не совпало {bad}")
        if bad:
            print("  libunity не та, под которую писались заплатки: собирай на Unity 2022.3.62f2")
            sys.exit(2)


if __name__ == "__main__":
    main()
