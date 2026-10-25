"""Add a bounded Horizon callback BEFORE unwinding null-reference exceptions.
The callback is optional; Android and older loaders retain normal behavior.
"""
from pathlib import Path
import argparse
MARK='ULTRAKILL NX null-reference trace ABI 1'
RAISE_MARK='ULTRAKILL NX generic raise trace ABI 1'

def patch_raise(text):
    if RAISE_MARK in text:return text
    anchor='''    NORETURN void Exception::Raise(Il2CppException* ex, MethodInfo* lastManagedFrame)
    {
        PrepareExceptionForThrow(ex, lastManagedFrame);'''
    replacement='''    NORETURN void Exception::Raise(Il2CppException* ex, MethodInfo* lastManagedFrame)
    {
        // ULTRAKILL NX generic raise trace ABI 1: bounded loader callback before unwind.
#if IL2CPP_TARGET_ANDROID
        static void (*trace)(const char*) = reinterpret_cast<void (*)(const char*)>(dlsym(RTLD_DEFAULT, "nxb_raise_trace"));
        if (trace && ex && ex->klass) trace(ex->klass->name);
#endif
        PrepareExceptionForThrow(ex, lastManagedFrame);'''
    if text.count(anchor)!=1:raise ValueError('Unexpected generic Raise; no edits made')
    return text.replace(anchor,replacement)
def patch(path):
    text=path.read_text(encoding='utf-8')
    if MARK in text:
        updated=patch_raise(text)
        if updated!=text:path.write_text(updated,encoding='utf-8')
        print('Null-reference + bounded generic raise trace ready:',path);return
    include='#include "il2cpp-config.h"'
    anchor='''    NORETURN void Exception::RaiseNullReferenceException(const utils::StringView<Il2CppChar>& msg)
    {
        Raise(GetNullReferenceException(msg));
    }'''
    replacement='''    NORETURN void Exception::RaiseNullReferenceException(const utils::StringView<Il2CppChar>& msg)
    {
        // ULTRAKILL NX null-reference trace ABI 1: callback returns before throw.
#if IL2CPP_TARGET_ANDROID
        static void (*trace)(void) = reinterpret_cast<void (*)(void)>(dlsym(RTLD_DEFAULT, "nxb_exception_trace"));
        if (trace) trace();
#endif
        Raise(GetNullReferenceException(msg));
    }'''
    if text.count(anchor)!=1 or text.count(include)!=1:raise ValueError('Unexpected matching Unity Exception.cpp; no edits made')
    backup=path.with_suffix(path.suffix+'.pre-nx-trace')
    if backup.exists():raise FileExistsError(backup)
    backup.write_bytes(path.read_bytes())
    path.write_text(patch_raise(text.replace(include,include+'\n#if IL2CPP_TARGET_ANDROID\n#include <dlfcn.h>\n#endif').replace(anchor,replacement)),encoding='utf-8')
    print('Patched:',path)
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('path',type=Path);args=ap.parse_args();patch(args.path)
