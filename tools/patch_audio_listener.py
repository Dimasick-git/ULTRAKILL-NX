"""Skip virtual audio spatial updates while a loading scene has no listener."""
import argparse,re
from pathlib import Path
MARK='// NX_AUDIO_LISTENER_GUARD'
def patch(generated):
    hits=[]
    pattern=re.compile(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void VirtualAudioManager_UpdateSources_m[0-9A-F]+ \(([^\n]*)\)\s*\n\{\n',re.M)
    for path in generated.glob('Assembly-CSharp*.cpp'):
        text=path.read_text(encoding='utf-8');m=pattern.search(text)
        if not m:continue
        hits.append(path)
        end=text.index('// Method Definition Index:',m.end())
        if MARK in text[m.end():end]:continue
        listener=re.search(r'AudioListener_t\w+\* (___\w+)',m[1]);assert listener,'Unexpected audio listener argument'
        # A direct null pointer check covers the confirmed pre-scene throw;
        # destroyed wrappers are handled by the game's normal listener lifecycle.
        body='\tif (!'+listener[1]+') return; '+MARK+'\n'
        text=text[:m.end()]+body+text[m.end():];path.write_text(text,encoding='utf-8')
    assert len(hits)==1,'Expected one virtual audio updater'
    print('PASS: virtual audio updater tolerates the empty loading scene')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('generated',type=Path);a=p.parse_args();patch(a.generated)
