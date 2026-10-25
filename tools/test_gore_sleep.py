from pathlib import Path
import argparse,re
from patch_gore_sleep import patch,MARKER

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('generated',type=Path);a=ap.parse_args()
    source=(a.generated/'Assembly-CSharp__13.cpp').read_text(encoding='utf-8-sig')
    engine=(a.generated/'UnityEngine.PhysicsModule.cpp').read_text(encoding='utf-8-sig')
    assert MARKER not in source
    changed=patch(source,engine);assert patch(changed,engine)==changed
    declaration=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void (Rigidbody_Sleep_m\w+) \([^\n]+\)',engine,re.M)
    clean=changed.replace(declaration[0]+';\n','',1)
    clean,n=re.subn(r'\n\t\t'+re.escape(MARKER)+r'\n\t\t'+declaration[1]+r'\(L_\d+, NULL\);','',clean)
    assert n==1 and clean==source,'Other gameplay code changed'
    print('PASS: only sleep after existing zero-velocity/disable-collisions; exact original gameplay body/pool launch retained; idempotent')
