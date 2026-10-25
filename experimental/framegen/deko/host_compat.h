#pragma once
#include <cwchar>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#ifndef _countof
#define _countof(array) (sizeof(array)/sizeof((array)[0]))
#endif
// SDK uses Microsoft's bounds-checked wchar overloads. Keep upstream files
// byte-identical and provide the two forms at the platform boundary.
inline int nx_ffx_wcscpy_s(wchar_t* dst,size_t capacity,const wchar_t* src){
 if(!dst||!capacity||!src)return EINVAL;
 size_t length=std::wcslen(src);
 if(length>=capacity){dst[0]=0;return ERANGE;}
 std::wmemcpy(dst,src,length+1);return 0;
}
template<size_t N> inline int nx_ffx_wcscpy_s(wchar_t (&dst)[N],const wchar_t* src){
 return nx_ffx_wcscpy_s(dst,N,src);
}
#define wcscpy_s nx_ffx_wcscpy_s
#include <FidelityFX/host/ffx_types.h>
// Upstream's opaque context budget assumes 16-bit Windows wchar_t. Horizon
// uses 32-bit wchar_t; FfxPipelineState contains many wchar name arrays. Grow
// the opaque allocation instead of changing wchar ABI or suppressing asserts.
// Every consumer of the host archive must include this adapter first.
#undef FFX_SDK_DEFAULT_CONTEXT_SIZE
#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 128 * ((sizeof(wchar_t)+1)/2))
