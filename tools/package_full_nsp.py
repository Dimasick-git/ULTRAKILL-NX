"""Build one DBI installer with installed RomFS assets and optional SD support.

No keys, saves, personal preferences, compiler caches or SDKs enter the NSP.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
from verify_full_nsp import verify,npdm_policy

def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def stage(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    try:os.link(src,dst)
    except OSError:shutil.copy2(src,dst)
def support_allowed(name):
    return name.startswith(('atmosphere/contents/0000000000534C56/','atmosphere/exefs_patches/SaltyNX_Fixes/')) or name in {
        'SaltySD/exceptions.txt','SaltySD/saltynx_bootstrap.elf','SaltySD/saltynx_bootstrap32.elf',
        'SaltySD/saltynx_core.elf','SaltySD/saltynx_core32.elf','SaltySD/flags/log.flag'}

def main():
    p=argparse.ArgumentParser()
    for name in ('game','support','keys','hacbrewpack','nacptool','work','output'):p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args();root=Path(__file__).resolve().parents[1]
    assert not a.work.exists(),'Use a new, empty work directory'
    required=['ultrakill_nx.nro','libmain.so','libunity.so','libil2cpp.so','offsets.ini','assets/bin/Data/globalgamemanagers','assets/bin/Data/Managed/Metadata/global-metadata.dat','assets/NxPort/nx_fsr.bundle']
    assert all((a.game/n).is_file() for n in required),'Incomplete game layout'
    assert not list((a.game/'assets/bin/Data').rglob('*.split0')), 'Join Unity split assets before building read-only RomFS'
    assert all((a.game/'assets/bin/Data'/name).is_file() for name in ('globalgamemanagers.assets','sharedassets0.assets'))
    assert b'ultrakill_nx r35' in (a.game/'ultrakill_nx.nro').read_bytes()
    assert b'nxb_touch' in (a.game/'libil2cpp.so').read_bytes()
    boot=json.loads((a.game/'assets/NxPort/boot-content.json').read_text(encoding='utf-8'))
    assert boot['source']=='ported-game'
    from stage_full_game import BOOT_FILES,RUNTIME_FILES
    assert set(boot['files'])==set(BOOT_FILES+RUNTIME_FILES)
    assert all(digest(a.game/'assets/bin/Data'/name)==expected for name,expected in boot['files'].items()), 'Boot graph/metadata provenance mismatch'
    a.work.mkdir(parents=True);rom=a.work/'romfs';exefs=a.work/'exefs';control=a.work/'control'
    exefs.mkdir();control.mkdir();rom.mkdir()
    for src in sorted(a.game.rglob('*')):
        if not src.is_file():continue
        rel=src.relative_to(a.game).as_posix()
        if rel.startswith(('assets/','licenses/')) or rel in required[:5]:stage(src,rom/'ultrakill'/rel)
    files=[s for s in a.support.rglob('*') if s.is_file() and support_allowed(s.relative_to(a.support).as_posix())]
    # A partial first-time support install must never activate the sysmodule.
    files.sort(key=lambda s:(s.name=='boot2.flag',s.relative_to(a.support).as_posix()))
    assert files and files[-1].name=='boot2.flag'
    lines=[]
    for src in files:
        rel=src.relative_to(a.support).as_posix();stage(src,rom/'support'/rel)
        lines.append(f'{src.stat().st_size} {digest(src)} {rel}\n')
    (rom/'support.list').write_bytes(''.join(lines).encode())
    for name in ('licenses/SaltyNX-UPSTREAM-README.md',):
        if (a.support/name).is_file():stage(a.support/name,rom/'documentation'/name)
    stage(root/'docs/INSTALL_NSP_RU.md',rom/'documentation/INSTALL_NSP_RU.md')
    shutil.copy2(root/'forwarder/hbl.nso',exefs/'main');shutil.copy2(root/'forwarder/hbl.npdm',exefs/'main.npdm')
    subprocess.run([str(a.nacptool),'--create','ULTRAKILL - NX','Dimasick-git','r35',str(control/'control.nacp')],check=True)
    shutil.copy2(root/'forwarder/icon.jpg',control/'icon_AmericanEnglish.dat')
    manifest={'version':'r35','title_id':'01005CE3B1A40000','storage':'title RomFS; SD saves/config/cache',
        'launcher':{'main':digest(exefs/'main')},
        'npdm_policy_sha256':npdm_policy((exefs/'main.npdm').read_bytes()),
        'files':{s.relative_to(rom).as_posix():{'bytes':s.stat().st_size,'sha256':digest(s)} for s in sorted(rom.rglob('*')) if s.is_file()}}
    (rom/'install-manifest.json').write_bytes((json.dumps(manifest,indent=2,ensure_ascii=False)+'\n').encode())
    print(f'Payload ready: {len(manifest["files"])} files, {sum(r["bytes"] for r in manifest["files"].values())} bytes',flush=True)
    command=[str(a.hacbrewpack),'-k',str(a.keys),'--exefsdir',str(exefs),'--controldir',str(control),'--romfsdir',str(rom),
        '--nspdir',str(a.work/'nsp'),'--ncadir',str(a.work/'nca'),'--tempdir',str(a.work/'temp'),'--backupdir',str(a.work/'backup'),
        '--nologo','--titleid','01005CE3B1A40000','--titlename','ULTRAKILL - NX','--titlepublisher','Dimasick-git']
    with (a.work/'hacbrewpack.log').open('w',encoding='utf-8') as log:subprocess.run(command,cwd=a.work,stdout=log,stderr=subprocess.STDOUT,check=True)
    outputs=list((a.work/'nsp').glob('*.nsp'));assert len(outputs)==1
    report=verify(outputs[0],a.keys)
    assert report['bytes']<0xffffffff,'Installer does not fit one FAT32 file'
    a.output.parent.mkdir(parents=True,exist_ok=True)
    temporary=a.output.with_suffix('.nsp.tmp');shutil.copy2(outputs[0],temporary)
    assert digest(temporary)==report['sha256'];temporary.replace(a.output)
    report['file']=str(a.output.resolve())
    a.output.with_suffix('.verified.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps(report,indent=2),flush=True)
if __name__=='__main__':main()
