#!/usr/bin/env python3
"""Проверки derive_offsets.py без настоящей сборки игры.

    python test_derive_offsets.py [путь к любому aarch64 ELF с символами]

1. Поиск глобала JavaVM* на последовательности, снятой с JNI_OnLoad
   libil2cpp Papers, Please (pp_il2cpp.h): ответ должен быть 0xf58410.
2. Если передан ELF (например, loader/ultrakill_nx.elf) -- разбор символов и
   чтение по виртуальному адресу.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import derive_offsets as d  # noqa: E402


class FakeElf:
    def __init__(self, base, words):
        self.base, self.code = base, struct.pack(f"<{len(words)}I", *words)

    def read_va(self, va, n):
        off = va - self.base
        return self.code[off:off + n].ljust(n, b"\0")


def enc_adrp(rd, pc, target):
    imm = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12
    imm &= (1 << 21) - 1
    return 0x90000000 | ((imm & 3) << 29) | ((imm >> 2) << 5) | rd


def enc_bl(pc, target):
    return 0x94000000 | (((target - pc) >> 2) & 0x3FFFFFF)


def test_javavm():
    base = 0x5F2990
    words = [
        0xA9BF4FFE,                          # stp x30, x19, [sp, #-0x10]!
        0xD503201F,                          # nop
        0xD503201F,                          # nop
        0xAA0003F3,                          # mov x19, x0
        0xD503201F, 0xD503201F, 0xD503201F,  # nop x3
        enc_bl(base + 0x1C, 0xE48E70),       # bl  -> x0 испорчен
        0xD503201F,                          # nop
        enc_adrp(8, base + 0x24, 0xF58000),  # adrp x8, #0xf58000
        0xD503201F,                          # nop
        0xF9020913,                          # str x19, [x8, #0x410]
        0xD65F03C0,                          # ret
    ]
    got = d.find_javavm_store(FakeElf(base, words), base)
    assert got == 0xF58410, hex(got or 0)

    # возврат функции в x0 после bl -- не JavaVM, ловить нельзя
    words2 = [0xD503201F, enc_bl(base + 4, 0xE48E70), enc_adrp(8, base + 8, 0xF58000),
              0xF9020900, 0xD65F03C0]          # str x0, [x8, #0x410]
    assert d.find_javavm_store(FakeElf(base, words2), base) is None
    print("javavm: ok")


def test_elf(path):
    e = d.Elf(path)
    syms = e.symbols()
    assert "main" in syms, "в ELF нет символа main"
    va, size = syms["main"]
    head = e.read_va(va, 8)
    assert len(head) == 8
    print(f"elf: ok ({len(syms)} символов, main @0x{va:x}, build-id {e.build_id()})")


if __name__ == "__main__":
    test_javavm()
    if len(sys.argv) > 1:
        test_elf(sys.argv[1])
