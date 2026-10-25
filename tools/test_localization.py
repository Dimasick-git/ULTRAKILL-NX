"""Check translation assembly and runtime TSV escaping without private game data."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from build_loc_source import fnv

TOOLS = Path(__file__).resolve().parent

def run(*args, success=True):
    p = subprocess.run([sys.executable, *map(str, args)], capture_output=True)
    assert (p.returncode == 0) == success, p.stdout + p.stderr

def put(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    for folder in ('chunks', 'ref', 'de', 'merged', 'lang'):
        (root / folder).mkdir()
    key = '0123456789abcdef'
    en = '<b>CHANGE {0}</b>\n ready'
    tr = '<b>ÄNDERN {0}</b>\n bereit\\\t'
    put(root/'source.json', [{'id': key, 'en': en}])
    (root/'chunks/000.txt').write_text(key+'\ttext\n', encoding='utf-8')
    put(root/'ref/000.json', {key: 'reference'})
    put(root/'de/glossary.json', {'CHANGE': 'ÄNDERN'})
    base = [TOOLS/'loc_chunks.py', 'check', '--source', root/'source.json',
            '--chunks', root/'chunks', '--loc', root/'de', '--reference', root/'ref']
    run(*base, success=False)  # a whole missing portion must fail
    put(root/'de/000.json', {key: tr})
    run(*base)  # ordinary German umlauts are not typewriter commands
    put(root/'de/000.json', {key: tr.replace('{0}', '{1}')})
    run(*base, success=False)
    put(root/'de/000.json', {key: tr})
    run(TOOLS/'loc_chunks.py', 'merge', '--loc', root/'de', '-o', root/'merged/de.json')
    assert json.loads((root/'merged/de.json').read_text(encoding='utf-8')) == {key: tr}
    invert = 'Invert X Axis'
    put(root/'source.json', [{'id': key, 'en': en}, {'id': fnv(invert), 'en': invert}])
    put(root/'merged/de.json', {key: tr, fnv(invert): 'X-Achse umkehren'})
    run(TOOLS/'build_lang.py', '--source', root/'source.json', '--loc', root/'merged', '--out', root/'lang')
    row = next(s for s in (root/'lang/de.tsv').read_text(encoding='utf-8').splitlines() if s.startswith(key))
    escaped = row.split('\t', 1)[1]
    decoded, i = '', 0
    while i < len(escaped):
        c = escaped[i]
        if c == '\\':
            i += 1
            c = {'n':'\n', 'r':'\r', 't':'\t'}.get(escaped[i], escaped[i])
        decoded += c
        i += 1
    assert decoded == tr  # matches NxLoc.Read, including literal backslash + tab
    assert fnv(invert.upper()) + '\tX-ACHSE UMKEHREN' in (root/'lang/de.tsv').read_text(encoding='utf-8')
    put(root/'de/001.json', {key: tr})
    run(TOOLS/'loc_chunks.py', 'merge', '--loc', root/'de', '-o', root/'merged/de.json', success=False)
print('PASS: missing portions, placeholders, glossary exclusion, duplicate IDs, TSV round trip')
