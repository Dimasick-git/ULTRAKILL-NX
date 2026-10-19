"""Guard GunControl.Update's optional desktop mouse after Unity IL2CPP export.

Keep the gamepad continuation: returning from Update would skip weapon logic.
Fail closed if a different game export no longer matches the audited method.
"""
from pathlib import Path
import re
import sys

MARKER = '// NX: absent desktop mouse means zero scroll; continue gamepad logic.'
METHOD = 'GunControl_Update_m42C1F4A5DBCE973DC97D433A91708F13418BEF83'

def patch(text):
    start = text.index('IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void ' + METHOD + ' (')
    end = text.index('\nIL2CPP_EXTERN_C ', start + 1)
    body = text[start:end]
    if MARKER in body:
        return text
    anchor = '\t\tL_195 = Mouse_get_current_m410C1F9ABC5AA77353574E8815F7E63289707986_inline(NULL);\n'
    assert body.count(anchor) == 1, 'Unexpected GunControl mouse expression'
    assert body.count('IL_060a:') == 1
    assert re.search(r'IL_060a:\s*\{\s*PlayerInput_', body)
    guard = '\t\t' + MARKER + '\n\t\tif (!L_195) { V_2 = 0.0f; goto IL_060a; }\n'
    body = body.replace(anchor, anchor + guard)
    return text[:start] + body + text[end:]

def main():
    root = Path(sys.argv[1])
    matches = [p for p in root.glob('Assembly-CSharp*.cpp')
               if 'void ' + METHOD + ' (' in p.read_text(encoding='utf-8')]
    assert len(matches) == 1, f'Expected one GunControl definition, got {matches}'
    path = matches[0]
    old = path.read_text(encoding='utf-8')
    new = patch(old)
    if new != old:
        backup = path.with_suffix('.cpp.before-mouse-guard')
        if not backup.exists(): backup.write_text(old, encoding='utf-8')
        path.write_text(new, encoding='utf-8')
    print('PASS: optional mouse guard, gamepad continuation retained:', path.name)

if __name__ == '__main__': main()
