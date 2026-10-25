"""Keep cached C++ objects when IL2CPP changed only method-index comments.

Run after all source patches. Identifiers, executable code, literals and source
line counts must match exactly; only line endings and the generated index
comment numbers can differ. No metadata or registration files are reused.
"""
from pathlib import Path
import argparse,re

INDEX=re.compile(rb'(?m)^(// Method Definition Index: )\d+([ \t]*)$')
def normalized(data):return INDEX.sub(rb'\1#\2',data.replace(b'\r\n',b'\n'))
def reuse(previous,generated):
    restored=0
    for old in previous.glob('*.cpp'):
        if old.name.startswith('Il2Cpp') or 'Metadata' in old.name or 'Registration' in old.name:continue
        new=generated/old.name
        if not new.is_file():continue
        before=old.read_bytes();after=new.read_bytes()
        if before!=after and normalized(before)==normalized(after):
            temp=new.with_name(new.name+'.nx-tmp');temp.write_bytes(before);temp.replace(new);restored+=1
    print(f'PASS: reused {restored} files with byte-identical code; only generated index comments differed')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--previous',required=True,type=Path);p.add_argument('--generated',required=True,type=Path)
    a=p.parse_args();reuse(a.previous,a.generated)
