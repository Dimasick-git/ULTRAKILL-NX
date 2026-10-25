"""Report bounds failures before the original IL2CPP throw; valid accesses stay silent."""
from pathlib import Path
import argparse

MARK='ULTRAKILL NX bounds source ABI 1'
OLD='''#define IL2CPP_ARRAY_BOUNDS_CHECK(index, length) \\
    do { \\
        if (((uint32_t)(index)) >= ((uint32_t)length)) il2cpp_codegen_raise_index_out_of_range_exception(); \\
    } while (0)'''
NEW='''// ULTRAKILL NX bounds source ABI 1
extern "C" void nx_il2cpp_bounds_site(const char*, int, uint32_t, uint32_t);
#define IL2CPP_ARRAY_BOUNDS_CHECK(index, length) \\
    do { \\
        const uint32_t nx_index = (uint32_t)(index), nx_length = (uint32_t)(length); \\
        if (nx_index >= nx_length) { \\
            nx_il2cpp_bounds_site(__FILE__, __LINE__, nx_index, nx_length); \\
            il2cpp_codegen_raise_index_out_of_range_exception(); \\
        } \\
    } while (0)'''
BRIDGE='''// ULTRAKILL NX bounds source ABI 1
extern "C" __attribute__((noinline)) void nx_il2cpp_bounds_site(const char* file, int line, uint32_t index, uint32_t length)
{
#if IL2CPP_TARGET_ANDROID
    using Report = void (*)(const char*, int, uint32_t, uint32_t, uintptr_t);
    static Report report = reinterpret_cast<Report>(dlsym(RTLD_DEFAULT, "nxb_bounds_site"));
    if (report) report(file, line, index, length, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
#endif
}

'''

def patch(root):
    header=root/'libil2cpp/codegen/il2cpp-codegen-il2cpp.h'
    source=root/'libil2cpp/vm/Exception.cpp'
    h=header.read_text(encoding='utf-8');c=source.read_text(encoding='utf-8')
    if MARK in h and MARK in c:return
    anchor='namespace il2cpp\n{\nnamespace vm\n{'
    assert h.count(OLD)==1 and c.count(anchor)==1 and '#include <dlfcn.h>' in c, 'Apply exception trace first; unexpected runtime'
    header.write_text(h.replace(OLD,NEW),encoding='utf-8')
    source.write_text(c.replace(anchor,BRIDGE+anchor),encoding='utf-8')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('root',type=Path);a=ap.parse_args();patch(a.root);print('PASS: bounded index diagnostics; original throw retained')
