"""Release the compositor's owned render textures when its scene destroys it."""
from pathlib import Path
import argparse,re
MARK='// NX_RENDER_OWNER_TEARDOWN'
def patch(text,detach=None):
    pattern=re.compile(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void PostProcessV2_Handler_OnDestroy_m[0-9A-F]+ \([^\n]*\)\s*\n\{\n',re.M)
    m=pattern.search(text)
    if not m:return text,False
    end=text.index('\n}\n',m.end())+3;body=text[m.start():end]
    if MARK in body and (detach is None or "// NX_RENDER_DETACH" in body):return text,True
    release=re.search(r'\b(PostProcessV2_Handler_ReleaseTextures_m[0-9A-F]{40})\s*\(',text)
    assert release,'Original texture teardown method missing'
    assert body.count('\t\treturn;')==1,'Unexpected compositor destructor'
    if MARK not in body:
        body=body.replace('\t\treturn;',f'\t\t{release[1]}(__this, NULL); {MARK}\n\t\treturn;')
    declaration=''
    if detach:
        anchor=f'\t\t{release[1]}(__this, NULL);'
        assert anchor in body
        body=body.replace(anchor,f'\t\t{detach}((RuntimeObject*)__this, NULL); // NX_RENDER_DETACH\n'+anchor)
        declaration=f'IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void {detach} (RuntimeObject*, const RuntimeMethod*);\n'

    return text[:m.start()]+declaration+body+text[end:],True
def apply(generated):
    from patch_nx_localization import method_name
    detach=method_name(generated,"NxGameRenderTargets_Detach")
    hits=0
    for path in generated.glob('Assembly-CSharp*.cpp'):
        old=path.read_text(encoding='utf-8');new,found=patch(old,detach);hits+=found
        if new!=old:path.write_text(new,encoding='utf-8')
    assert hits==1,'Expected one compositor destructor'
    print('PASS: original camera callback unregistered, then owned render textures released on scene teardown')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('generated',type=Path);a=p.parse_args();apply(a.generated)
