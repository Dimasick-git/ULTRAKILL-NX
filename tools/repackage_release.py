#!/usr/bin/env python3
"""Merge the existing release and verified update on GitHub's runner.
Old downloads remain live until both replacements pass content/server hashes.
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib,json,os,re,shutil,subprocess,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
REPO=os.environ['GITHUB_REPOSITORY']
TAG=os.environ['PACKAGE_TAG']
PATCH=os.environ['PACKAGE_PATCH']
PATCH_SHA=os.environ['PACKAGE_PATCH_SHA'].lower()
match=re.fullmatch(r'ULTRAKILL_NX_(r\d+)_update\.zip',PATCH)
if not match:raise ValueError('Expected a versioned ULTRAKILL_NX update ZIP')
VERSION=match.group(1)
NAMES=['ULTRAKILL_NX_r1_1of2.zip','ULTRAKILL_NX_r1_2of2.zip']
WORK=ROOT/'release-work';INPUT=WORK/'input';GAME=WORK/'package';OUT=WORK/'output'
for p in (INPUT,GAME,OUT):p.mkdir(parents=True,exist_ok=True)

def gh(*args):
    return subprocess.check_output(['gh',*args],text=True)
def api(path,*args):
    text=gh('api',f'repos/{REPO}/'+path,*args)
    return json.loads(text) if text.strip() else None
def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def manifest(data):
    return dict((name,digest) for digest,name in (l.split('  ',1) for l in data.decode('utf-8-sig').splitlines() if l))
def safe_name(name):
    if name.startswith('/') or '..' in name.split('/') or '\\' in name:
        raise ValueError('Unsafe ZIP member: '+name)
    return name
def extract_verified(path, expected=None, only_game=False):
    actual={};names=[]
    with zipfile.ZipFile(path) as archive:
        for item in archive.infolist():
            if item.is_dir():continue
            name=safe_name(item.filename);names.append(name)
            digest=hashlib.sha256()
            keep=not only_game or (name.startswith('switch/ultrakill/') and not name.startswith('switch/ultrakill/il2cpp/'))
            dest=GAME/name
            if keep:dest.parent.mkdir(parents=True,exist_ok=True)
            out=dest.open('wb') if keep else None
            try:
                with archive.open(item) as src:
                    while chunk:=src.read(1024*1024):
                        digest.update(chunk)
                        if out:out.write(chunk)
            finally:
                if out:out.close()
            actual[name]=digest.hexdigest()
            if expected and name!='SHA256SUMS.txt':assert actual[name]==expected[name],name
    return actual,names

assert re.fullmatch('[a-f0-9]{64}',PATCH_SHA)
assert Path(PATCH).name==PATCH and PATCH.endswith('.zip')
release=api(f'releases/tags/{TAG}')
old_assets={a['name']:a for a in release['assets']}
for name in NAMES+[PATCH]:
    gh('release','download',TAG,'--repo',REPO,'--pattern',name,'--dir',str(INPUT))
    expected=PATCH_SHA if name==PATCH else old_assets[name]['digest'].removeprefix('sha256:')
    assert sha(INPUT/name)==expected,name
print('All downloaded archives match their SHA-256.',flush=True)

groups=[];base={}
for name in NAMES:
    actual,members=extract_verified(INPUT/name)
    for member,digest in actual.items():
        if member in base:assert base[member]==digest,member
        base[member]=digest
    groups.append(set(members))
base_manifest=manifest((GAME/'SHA256SUMS.txt').read_bytes())
assert {k:v for k,v in base.items() if k!='SHA256SUMS.txt'}==base_manifest
with zipfile.ZipFile(INPUT/PATCH) as archive:
    patch_manifest=manifest(archive.read('SHA256SUMS.txt'))
extract_verified(INPUT/PATCH,patch_manifest,only_game=True)
shutil.copyfile(ROOT/'docs/INSTALL_RU.txt',GAME/'README_RU.txt')
expected={p.relative_to(GAME).as_posix():sha(p) for p in GAME.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt'}
(GAME/'SHA256SUMS.txt').write_text(''.join(f'{v}  {k}\n' for k,v in sorted(expected.items())),encoding='utf-8')
expected['SHA256SUMS.txt']=sha(GAME/'SHA256SUMS.txt')
for name in expected.keys()-(groups[0]|groups[1]):groups[0].add(name)
assert groups[0]|groups[1]==expected.keys()
assert groups[0]&groups[1]=={'README_RU.txt','SHA256SUMS.txt'}
for name in NAMES:(INPUT/name).unlink() # own temporary downloads; free space before compression

def pack(i):
    path=OUT/NAMES[i]
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as archive:
        for name in sorted(groups[i]):archive.write(GAME/name,name)
    assert path.stat().st_size<2*1024**3
    with zipfile.ZipFile(path) as archive:
        actual={}
        for item in archive.infolist():
            with archive.open(item) as src:actual[item.filename]=hashlib.file_digest(src,'sha256').hexdigest()
        assert actual=={k:expected[k] for k in groups[i]}
    return {'name':path.name,'bytes':path.stat().st_size,'sha256':sha(path)}
with ThreadPoolExecutor(max_workers=2) as pool:report=list(pool.map(pack,(0,1)))
print('Both full archives passed per-file hashes and can merge in either order.',flush=True)

run=os.environ.get('GITHUB_RUN_ID','manual')+'-'+os.environ.get('GITHUB_RUN_ATTEMPT','1')
for i,item in enumerate(report,1):
    temporary=f'ULTRAKILL_NX_r1_ci_{run}_{i}of2.zip'
    os.link(OUT/item['name'],OUT/temporary)
    gh('release','upload',TAG,str(OUT/temporary),'--repo',REPO)
    item['temporary']=temporary
current=api(f'releases/tags/{TAG}')
assets={a['name']:a for a in current['assets']}
for item in report:
    remote=assets[item['temporary']]
    assert remote['state']=='uploaded' and remote['size']==item['bytes']
    assert remote['digest']=='sha256:'+item['sha256']
for item in report:
    if item['name'] in assets:api(f'releases/assets/{assets[item["name"]]["id"]}','--method','DELETE')
    api(f'releases/assets/{assets[item["temporary"]]["id"]}','--method','PATCH','-f','name='+item['name'])
checksums=OUT/'SHA256SUMS.txt'
checksums.write_text(''.join(f'{i["sha256"]}  {i["name"]}\n' for i in report))
gh('release','upload',TAG,str(checksums),str(GAME/'README_RU.txt'),'--repo',REPO,'--clobber')

body=re.sub(r'сборка \*\*r\d+\*\*','сборка **'+VERSION+'**',current['body'])
body=body.replace('Полные ZIP ниже остаются r22.','Полные ZIP ниже уже содержат r23; маленький архив нужен только для обновления существующей установки.')
body=re.sub(r'Полные ZIP ниже уже содержат r\d+;',f'Полные ZIP ниже уже содержат {VERSION};',body)
body=body.replace('.\n### Скачать','.\n\n### Скачать')
for item in report:
    body=body.replace(old_assets[item['name']]['digest'].removeprefix('sha256:'),item['sha256'])
    lines=body.splitlines()
    for index,line in enumerate(lines):
        if line.startswith('| [') and item['name'] in line:
            size=f"{item['bytes']/1e9:.2f}".replace('.',',')
            lines[index]=line.rsplit('|',2)[0]+f'| {size} ГБ |'
    body='\n'.join(lines)+'\n'
bullet='- Автоматический GC с остановкой потоков Horizon и сохранением полного регистрового контекста.\n'
if bullet not in body:body=body.replace('### В этой сборке\n\n','### В этой сборке\n\n'+bullet)
notes=OUT/'notes.md';notes.write_text(body,encoding='utf-8')
gh('release','edit',TAG,'--repo',REPO,'--notes-file',str(notes),'--prerelease=false','--latest')
api(f'git/refs/tags/{TAG}','--method','PATCH','-f','sha='+os.environ['GITHUB_SHA'],'-F','force=true')
result={'version':VERSION,'commit':os.environ['GITHUB_SHA'],'archives':report,'nro_sha256':expected['switch/ultrakill/ultrakill_nx.nro']}
result_path=OUT/(VERSION+'-packaging-report.json');result_path.write_text(json.dumps(result,indent=2)+'\n')
gh('release','upload',TAG,str(result_path),'--repo',REPO,'--clobber')
print(json.dumps(result),flush=True)
