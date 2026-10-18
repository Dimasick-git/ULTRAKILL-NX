"""Preserve IL2CPP NullCheck behavior while reporting exact generated source sites.
Apply after Android export, before compile-cpp. No callback on non-null values.
"""
from pathlib import Path
import argparse
def patch(root):
    header=root/'libil2cpp/codegen/il2cpp-codegen-il2cpp.h'
    cpp=root/'libil2cpp/vm/Exception.cpp'
    h=header.read_text(encoding='utf-8');c=cpp.read_text(encoding='utf-8')
    marker='ULTRAKILL NX nullcheck source ABI 1'
    if marker in h and marker in c:print('Already patched:',root);return
    old='''inline void NullCheck(void* this_ptr)
{
    if (this_ptr != NULL)
        return;

    il2cpp_codegen_raise_null_reference_exception();
}'''
    new='''// ULTRAKILL NX nullcheck source ABI 1
extern "C" void nx_il2cpp_null_site(const char*, int);
inline void NxNullCheckImpl(void* this_ptr, const char* file, int line)
{
    if (this_ptr != NULL)
        return;
    nx_il2cpp_null_site(file, line);
    il2cpp_codegen_raise_null_reference_exception();
}
#define NullCheck(ptr) NxNullCheckImpl((ptr), __FILE__, __LINE__)'''
    anchor='namespace il2cpp\n{\nnamespace vm\n{'
    bridge='''// ULTRAKILL NX nullcheck source ABI 1
extern "C" void nx_il2cpp_null_site(const char* file, int line)
{
#if IL2CPP_TARGET_ANDROID
    static void (*report)(const char*, int) = reinterpret_cast<void (*)(const char*, int)>(dlsym(RTLD_DEFAULT, "nxb_nullcheck_site"));
    if (report) report(file, line);
#endif
}

'''
    if h.count(old)!=1 or c.count(anchor)!=1 or '#include <dlfcn.h>' not in c:
        raise ValueError('Unexpected matching runtime; apply exception trace first')
    for path in (header,cpp):
        backup=path.with_suffix(path.suffix+'.pre-nx-site');
        if backup.exists():raise FileExistsError(backup)
        backup.write_bytes(path.read_bytes())
    header.write_text(h.replace(old,new),encoding='utf-8')
    cpp.write_text(c.replace(anchor,bridge+anchor),encoding='utf-8')
    print('Patched:',root)
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('root',type=Path);args=ap.parse_args();patch(args.root)
