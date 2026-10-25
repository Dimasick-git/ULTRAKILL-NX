"""Complete stain reassignment when its previous cosmetic parent is gone."""
import argparse,re
from pathlib import Path
MARKER='// NX_STAIN_TRANSFER_MISSING_OLD_PARENT'

def patch(text):
    if MARKER in text:return text
    match=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void BloodstainParent_AddStain_m[^\n]+\n\{[\s\S]*?(?=^// Method Definition Index:)',text,re.M)
    if not match:raise ValueError('Missing AddStain definition')
    old=re.search(r'\t\tNullCheck\(L_10\);\n\t\t(BloodstainParent_RemoveStain_m\w+\(L_10, L_11, NULL\);)',match[0])
    if not old:raise ValueError('Unknown previous-parent detach path')
    # Continue into the existing IL_0025 assignment. Returning early here would
    # retain the stale ownership and repeat the failure on every reuse.
    replacement='\t\t'+MARKER+'\n\t\tif (L_10) '+old[1]
    body=match[0][:old.start()]+replacement+match[0][old.end():]
    return text[:match.start()]+body+text[match.end():]

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('file',type=Path);a=parser.parse_args()
    text=a.file.read_text(encoding='utf-8-sig');a.file.write_text(patch(text),encoding='utf-8')
    print('Stain transfer: detach existing old parent; continue reassignment if old parent is null')
