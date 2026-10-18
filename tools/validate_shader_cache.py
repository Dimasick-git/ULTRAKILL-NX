#!/usr/bin/env python3
"""Validate the real ARM64 Shader cache accessors against matching Unity symbols.

Checks that both tiny functions access the same writable int, the setter stores
its argument, the getter returns it, and the library initializes it to -1.
"""
import argparse
import struct
from derive_offsets import Elf


def target(lib, address, store):
    a, op, ret = struct.unpack('<3I', lib.read_va(address, 12))
    assert a & 0x9F00001F == 0x90000008, 'expected adrp x8'
    imm = ((a >> 29) & 3) | (((a >> 5) & 0x7FFFF) << 2)
    if imm & (1 << 20):
        imm -= 1 << 21
    expected = 0xB9000100 if store else 0xB9400100
    assert op & 0xFFC003FF == expected, 'expected str/ldr w0,[x8,#offset]'
    assert ret == 0xD65F03C0, 'expected ret'
    return (address & ~0xFFF) + (imm << 12) + ((op >> 10) & 0xFFF) * 4


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--lib', required=True)
    ap.add_argument('--sym', required=True)
    args = ap.parse_args()
    lib, sym = Elf(args.lib), Elf(args.sym)
    assert lib.build_id() == sym.build_id(), 'build-id mismatch'
    syms = sym.symbols()
    setter = syms['_ZN6Shader27SetMaxChunksRuntimeOverrideEi']
    getter = syms['_ZN6Shader27GetMaxChunksRuntimeOverrideEv']
    assert setter[1] == getter[1] == 12, 'unexpected accessor size'
    dest = target(lib, setter[0], True)
    assert dest == target(lib, getter[0], False), 'accessors use different globals'
    assert struct.unpack('<i', lib.read_va(dest, 4))[0] == -1
    writable = False
    for i in range(lib.e_phnum):
        off = lib.e_phoff + i * lib.e_phentsize
        typ, flags, _, va, _, _, memsz = struct.unpack_from('<IIQQQQQ', lib.data, off)
        if typ == 1 and va <= dest <= va + memsz - 4 and flags & 2:
            writable = True
    assert writable, 'cache override is not writable'
    print(f'Shader cache accessors OK: set=0x{setter[0]:x}, get=0x{getter[0]:x}, int=0x{dest:x}')


if __name__ == '__main__':
    main()
