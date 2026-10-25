"""Notify the native limiter immediately after an explicit graphics-menu choice."""
from pathlib import Path
import re,argparse
MARK='// NX_FPS_MENU_REQUEST'
def patch(text):
 pattern=r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void GraphicsSettings_SetFrameRateLimit_m[0-9A-F]+ \([^\n]+\)\s*\n\{'
 m=re.search(pattern,text,re.M)
 if not m:return text,False
 end=text.index('\n}',m.end());body=text[m.start():end]
 if MARK in body:return text,True
 call=re.search(r'(\t\tApplication_set_targetFrameRate_m[0-9A-F]+\((V_0|L_[0-9]+), NULL\);)',body)
 assert call,'Expected game FPS selection setter'
 hook=r"""
        { // NX_FPS_MENU_REQUEST
            typedef void (*NxFpsRequest)(int);
            static NxFpsRequest request=(NxFpsRequest)dlsym(RTLD_DEFAULT,"nxb_fps_request");
            if(request)request(V_0);
        }"""
 hook=hook.replace("request(V_0)","request("+call[2]+")")
 body=body[:call.end()]+hook+body[call.end():]
 result=text[:m.start()]+body+text[end:]
 if '#include <dlfcn.h>' not in result:result='#include <dlfcn.h>\n'+result
 return result,True
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('generated',type=Path);a=p.parse_args();hits=0
 for file in a.generated.glob('Assembly-CSharp*.cpp'):
  old=file.read_text(encoding='utf-8');new,found=patch(old);hits+=found
  if new!=old:file.write_text(new,encoding='utf-8')
 assert hits==1,'Expected one graphics FPS setter'
 print('PASS: menu request delivered immediately after game setter, before return')
