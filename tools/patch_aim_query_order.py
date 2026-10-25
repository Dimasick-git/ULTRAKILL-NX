"""Reject ineligible autoaim candidates before occlusion queries.

Move the original generated projection/filter blocks verbatim. Keep target
selection, score, candidate order, portal exclusion and physics unchanged.
"""
from pathlib import Path
import argparse
import re

MARKER = '// NX_AIM_FILTER_BEFORE_OCCLUSION'

def patch(text):
    if MARKER in text:
        return text
    match = re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void CameraFrustumTargeter_Update_m[^\n]+\n\{[\s\S]*?(?=^// Method Definition Index:)', text, re.M)
    if not match:
        raise ValueError('Missing autoaim Update implementation')
    body = match[0]
    anchor = '\nIL_0246:\n'
    start = body.index('\t{\n\t\tCamera_t', body.index('\nIL_02c6:\n'))
    end = body.index('\t{\n\t\tfloat L_159 = V_19;', start)
    filters = body[start:end]
    # Fail closed on a different export rather than rewriting unfamiliar code.
    assert filters.count('goto IL_03a3;') == 6
    assert 'Camera_WorldToViewportPoint_' in filters
    assert 'Vector3_Distance_' in filters
    assert 'float L_158 = V_4;' in filters
    assert body.index('PortalPhysicsV2_Raycast_', body.index(anchor)) < start
    changed = body[:start] + body[end:]
    changed = changed.replace(anchor, anchor + '\t' + MARKER + '\n' + filters, 1)
    return text[:match.start()] + changed + text[match.end():]

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    args = parser.parse_args()
    source = args.file.read_text(encoding='utf-8-sig')
    result = patch(source)
    args.file.write_text(result, encoding='utf-8')
    print('Autoaim: original candidate filters precede occlusion queries')
