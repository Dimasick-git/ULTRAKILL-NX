"""Keep the original world's MRT/depth bound before every world render.

Unity's cached RT dimensions do not prove that a native camera target binding
survived scene/graphics resets. Apply after export, before IL2CPP compilation.
"""
from pathlib import Path
import argparse,re
MARKER='// NX_WORLD_MRT_REBIND_AFTER_NESTED_RENDER'
LEGACY_MARKER='// NX_WORLD_MRT_REBIND'
def patch(text):
 if MARKER in text:return text
 if LEGACY_MARKER in text:
  # Upgrade r42's early binding; do not leave two repairs in the callback.
  legacy=r'\n        // NX_WORLD_MRT_REBIND\n[\s\S]*?\n        \}\n'
  text,n=re.subn(legacy,'\n',text,count=1)
  if n!=1:raise ValueError('Cannot identify legacy MRT patch')
 pattern=r'(IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void PostProcessV2_Handler_OnPreRenderCallback_\w+ \([^\n]*\)\s*\n\{)([\s\S]*?)(?=// Method Definition Index:)'
 match=re.search(pattern,text)
 if not match:raise ValueError('Missing OnPreRenderCallback implementation')
 body=match[0]
 # The world-camera branch is the only SetupRTs call in this callback.
 setup=re.search(r'\t\t(PostProcessV2_Handler_SetupRTs_\w+)\(__this, NULL\);',body)
 if not setup:raise ValueError('Missing world SetupRTs call')
 def symbol(name):
  hit=re.search(r'\b('+name+r'_m\w+)\s*\(',text)
  if not hit:raise ValueError('Missing IL2CPP function '+name)
  return hit[1]
 color=symbol('RenderTexture_get_colorBuffer');depth=symbol('RenderTexture_get_depthBuffer')
 # The array overload has the RenderBuffer[] argument L_98 in SetupRTs.
 hit=re.search(r'\b(Camera_SetTargetBuffers_m\w+)\(L_97, L_98, L_100, NULL\);',text)
 if not hit:raise ValueError('Missing original array SetTargetBuffers overload')
 target=hit[1]
 injected=f'''
        {MARKER}
        // RenderSkyboxes/portals may invoke Camera.Render recursively. Restore
        // world MRT only AFTER those callbacks, immediately before world draw.
        if (___0_cam == __this->___mainCam && __this->___mainCam && __this->___mainTex && __this->___reusableBufferA &&
            __this->___viewNormal && __this->___depthBuffer && __this->___buffers &&
            __this->___buffers->max_length >= 3) {{
            __this->___buffers->SetAt(0, {color}(__this->___mainTex, NULL));
            __this->___buffers->SetAt(1, {color}(__this->___reusableBufferA, NULL));
            __this->___buffers->SetAt(2, {color}(__this->___viewNormal, NULL));
            {target}(__this->___mainCam, __this->___buffers,
                {depth}(__this->___depthBuffer, NULL), NULL);
        }}
'''
 tail=re.search(r'(IL_048e:\s*\n\t\{\s*\n)(\t\treturn;)',body)
 if not tail:raise ValueError('Missing final callback return label IL_048e')
 body=body[:tail.end(1)]+injected+body[tail.end(1):]
 return text[:match.start()]+body+text[match.end():]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('cpp',type=Path);a=ap.parse_args()
 old=a.cpp.read_text(encoding='utf-8');new=patch(old)
 if new!=old:a.cpp.write_text(new,encoding='utf-8')
 assert patch(new)==new and new.count(MARKER)==1
 print('PASS: world MRT rebind patch present and idempotent')
if __name__=='__main__':main()
