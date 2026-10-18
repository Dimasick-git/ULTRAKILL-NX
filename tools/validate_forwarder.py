#!/usr/bin/env python3
"""Validate the launch-critical NPDM fields for the installed ULTRAKILL forwarder.

Run on the NPDM EXTRACTED FROM THE PACKAGED NSP as well as on the build input.
"""
import argparse
from pathlib import Path
import struct

TITLE = 0x01005CE3B1A40000


def validate(data):
    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]

    def u64(offset):
        return struct.unpack_from('<Q', data, offset)[0]

    def region(offset, size):
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError('NPDM region outside file')
        return data[offset:offset+size]

    if region(0, 4) != b'META':
        raise ValueError('Not an NPDM META')
    aci, aci_size, acid, acid_size = struct.unpack_from('<IIII', data, 0x70)
    region(aci, aci_size)
    region(acid, acid_size)
    if region(aci, 4) != b'ACI0' or region(acid+0x200, 4) != b'ACID':
        raise ValueError('Invalid ACI0/ACID magic')
    if u64(aci+0x10) != TITLE:
        raise ValueError('Wrong ACI program ID')
    if not u64(acid+0x210) <= TITLE <= u64(acid+0x218):
        raise ValueError('ACI program ID is outside ACID permitted ID range')
    if (data[0x0c] >> 1) & 7 != 3 or not data[0x0c] & 1:
        raise ValueError('Demand-memory forwarder requires 64-bit / 39-bit address space')
    if u32(0x14) != 0x02000000:
        raise ValueError('Expected 32 MiB system resource')
    if (u32(acid+0x20c) >> 2) & 0xf != 0:
        raise ValueError('Expected Application memory pool partition')
    for label, base, cap_field in [('ACI', aci, 0x30), ('ACID', acid, 0x230)]:
        cap_offset, cap_size = u32(base+cap_field), u32(base+cap_field+4)
        caps = struct.unpack('<'+'I'*(cap_size//4), region(base+cap_offset, cap_size))
        app_types = [(cap >> 14) & 7 for cap in caps if cap & 0x3fff == 0x1fff]
        if app_types != [1]:
            raise ValueError(f'{label}: application_type must be 1 (Application), got {app_types}')
        syscalls = set()
        for cap in caps:
            if cap & 0x1f == 0xf:
                index, mask = cap >> 29, (cap >> 5) & 0xffffff
                syscalls.update(index*24+i for i in range(24) if mask & (1 << i))
        if not {0x2c, 0x2d} <= syscalls:
            raise ValueError(f'{label}: Map/UnmapPhysicalMemory permission missing')
    return 'PASS: program ID/range, Application type/pool, 39-bit address space, 32 MiB resource, physical-map permissions'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('npdm', type=Path)
    a = ap.parse_args()
    print(validate(a.npdm.read_bytes()))


if __name__ == '__main__':
    main()
