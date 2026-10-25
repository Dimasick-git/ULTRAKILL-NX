"""Minimal public libnx platform definition for the pinned Jolt checkout.

No proprietary console configuration is used. Mutex/condition and ARM timer
implementations remain upstream; aligned allocation uses libnx's memalign.
"""
from pathlib import Path
import argparse,hashlib,subprocess

def main():
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);args=p.parse_args()
 revision=subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
 if revision!='01adafba08bddb650046a822983c52aa734fcadb':raise ValueError('Unexpected Jolt revision')
 path=args.source/'Jolt/Core/Core.h';text=path.read_text()
 edits=[('#elif defined(_WIN32) || defined(_WIN64)','#elif defined(__SWITCH__)\n\t#define JPH_PLATFORM_SWITCH\n#elif defined(_WIN32) || defined(_WIN64)'),
        ('#define JPH_PLATFORM_SWITCH\n','#define JPH_PLATFORM_SWITCH\n\t#include <alloca.h>\n'),
        ('#elif defined(JPH_PLATFORM_LINUX) || defined(JPH_PLATFORM_ANDROID) || defined(JPH_PLATFORM_MACOS) || defined(JPH_PLATFORM_IOS) || defined(JPH_PLATFORM_BSD)','#elif defined(JPH_PLATFORM_SWITCH) || defined(JPH_PLATFORM_LINUX) || defined(JPH_PLATFORM_ANDROID) || defined(JPH_PLATFORM_MACOS) || defined(JPH_PLATFORM_IOS) || defined(JPH_PLATFORM_BSD)')]
 for old,new in edits:
  if old=='#elif defined(_WIN32) || defined(_WIN64)' and '#define JPH_PLATFORM_SWITCH' in text:continue
  if new in text:continue
  if text.count(old)!=1:raise ValueError('Jolt platform patch context changed')
  text=text.replace(old,new)
 if path.read_bytes()!=text.encode():path.write_text(text,newline='\n')
 print('Jolt public Switch platform ready; Core.h SHA256',hashlib.sha256(path.read_bytes()).hexdigest())
 memory=args.source/'Jolt/Core/Memory.cpp';text=memory.read_text()
 marker='// libnx exposes memalign rather than posix_memalign.'
 if marker not in text:
  old='return _aligned_malloc(inSize, inAlignment);\n#else'
  new='return _aligned_malloc(inSize, inAlignment);\n#elif defined(JPH_PLATFORM_SWITCH)\n\t'+marker+'\n\treturn memalign(inAlignment, inSize);\n#else'
  if text.count(old)!=1:raise ValueError('Jolt allocator patch context changed')
  text=text.replace(old,new).replace('#include <stdlib.h>','#include <stdlib.h>\n#ifdef __SWITCH__\n#include <malloc.h>\n#endif')
  memory.write_text(text,newline='\n')
 print('Jolt allocator SHA256',hashlib.sha256(memory.read_bytes()).hexdigest())

if __name__=='__main__':main()
