"""Wrap the game's loading coroutine before its first Addressables request."""
import argparse,re
from pathlib import Path
from patch_nx_localization import method_name

MARK='// NX_SCENE_MEMORY'
def patch(gen):
    wrapper=method_name(gen,'NxSceneMemory_WrapSceneLoad')
    hits=[]
    pattern=re.compile(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR RuntimeObject\* SceneHelper_LoadSceneCoroutine_m[0-9A-F]+ \([^\n]+\)\s*\n\{\n',re.M)
    for path in gen.glob('Assembly-CSharp*.cpp'):
        text=path.read_text(encoding='utf-8');match=pattern.search(text)
        if not match:continue
        end=text.index('\n}\n',match.end())+3;body=text[match.start():end]
        if MARK in body:hits.append(path);continue
        returns=re.findall(r'\breturn ([A-Za-z0-9_]+);',body);assert len(returns)==1, 'Unexpected coroutine factory'
        body=body.replace('return '+returns[0]+';',f'return {wrapper}((RuntimeObject*){returns[0]}, (RuntimeObject*)__this, NULL); {MARK}')
        declaration=f'IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR RuntimeObject* {wrapper} (RuntimeObject*, RuntimeObject*, const RuntimeMethod*);\n'
        text=text[:match.start()]+declaration+body+text[end:];path.write_text(text,encoding='utf-8');hits.append(path)
    assert len(hits)==1, 'Expected one SceneHelper coroutine factory'
    print('PASS: scene cleanup wraps the original coroutine, preserving its loading blocker and continuation')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('generated',type=Path);a=ap.parse_args();patch(a.generated)
