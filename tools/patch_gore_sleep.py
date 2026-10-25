"""Let stopped non-colliding gibs sleep; pool activation keeps AddForce wakeup."""
from pathlib import Path
import argparse,re
MARKER='// NX_STOPPED_GORE_SLEEP'

def patch(text,engine):
    if MARKER in text:return text
    declaration=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void (Rigidbody_Sleep_m\w+) \([^\n]+\)',engine,re.M)
    assert declaration
    m=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void GoreSplatter_StopGore_m[^\n;]+\n\{[\s\S]*?(?=^// Method Definition Index:)',text,re.M)
    assert m
    body=m[0]
    anchor=re.search(r'\t\tRigidbody_set_detectCollisions_\w+\((L_\d+), \(bool\)0, NULL\);',body)
    assert anchor and body.count('Rigidbody_set_detectCollisions_')==1
    assert body.index('Rigidbody_set_velocity_')<anchor.start()
    changed=body[:anchor.end()]+'\n\t\t'+MARKER+'\n\t\t'+declaration[1]+'('+anchor[1]+', NULL);'+body[anchor.end():]
    result=text[:m.start()]+changed+text[m.end():]
    pos=re.search(r'^// Method Definition Index:',result,re.M).start()
    return result[:pos]+declaration[0]+';\n'+result[pos:]

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('generated',type=Path);a=ap.parse_args()
    p=a.generated/'Assembly-CSharp__13.cpp';engine=(a.generated/'UnityEngine.PhysicsModule.cpp').read_text(encoding='utf-8-sig')
    p.write_text(patch(p.read_text(encoding='utf-8-sig'),engine),encoding='utf-8')
    print('Stopped gore: zero velocity, disable collisions, sleep; original pool AddForce wakeup retained')
