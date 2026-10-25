#!/usr/bin/env python3
"""Keep freed Boehm heap blocks mapped when running under the Switch loader.

GC_unmap() returns long-unused free blocks to the OS with
mmap(addr, PROT_NONE, MAP_FIXED) and aborts unless the same address comes back.
The loader's mmap cannot remap an address in place, so the first unmap is an
abort(). r78 hit it as soon as incremental GC was enabled. With the nxbridge
present (nxb_gc_bridge_init exported) GC_unmap_old() does nothing; normal
Android keeps unmapping. Original copyright/license headers are retained.
Run after export, before compile-cpp.
"""
from pathlib import Path
import argparse

MARK = '/* Modified for ULTRAKILL NX by Dimasick-git, 2026: no unmapping under the Switch loader. */'

ANCHOR = '''GC_INNER void GC_unmap_old(void)
{
    word sz;
    unsigned short last_rec, threshold;
    int i;
'''

GUARD = ANCHOR + '''    ''' + MARK + '''
    static int nx_keep_mapped = -1;
    if (nx_keep_mapped < 0)
      nx_keep_mapped = dlsym(RTLD_DEFAULT, "nxb_gc_bridge_init") != NULL;
    if (nx_keep_mapped) return;
'''


def patch(path):
    text = path.read_text(encoding='utf-8')
    if MARK in text:
        print('Already patched:', path)
        return
    if text.count(ANCHOR) != 1:
        raise ValueError(f'Expected one GC_unmap_old anchor, found {text.count(ANCHOR)}')
    text = text.replace(ANCHOR, GUARD, 1)
    include = '#include "private/gc_priv.h"\n'
    if text.count(include) != 1:
        raise ValueError('Expected one gc_priv.h include')
    text = text.replace(include, include + '#include <dlfcn.h>\n', 1)
    backup = path.with_suffix(path.suffix + '.pre-nx-gc')
    if not backup.exists():
        backup.write_bytes(path.read_bytes())
    path.write_text(text, encoding='utf-8')
    print('Patched:', path)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='exported IL2CPP/external/bdwgc/allchblk.c')
    patch(ap.parse_args().source)
