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
    p.add_argument('--version',choices=('r52','r53','r54','r55','r56','r57','r58','r59','r60','r61','r62','r63','r64','r65','r66','r67','r68','r69','r70','r71','r72','r73','r74','r75','r76','r77','r78','r79','r80','r81','r82','r83','r84','r85','r86','r87','r88','r89','r90','r91','r92','r93'),default='r52')
    a=p.parse_args();root=Path(__file__).resolve().parents[1]
    assert not a.work.exists(),'Use a new, empty work directory'
    required=['ultrakill_nx.nro','libmain.so','libunity.so','libil2cpp.so','offsets.ini','assets/bin/Data/globalgamemanagers','assets/bin/Data/Managed/Metadata/global-metadata.dat','assets/NxPort/nx_fsr.bundle']
    assert all((a.game/n).is_file() for n in required),'Incomplete game layout'
    assert (a.game/'assets/NxPort/nx_fsr3.bundle').is_file(), 'FSR 3.1 bundle missing'
    assert not list((a.game/'assets/bin/Data').rglob('*.split0')), 'Join Unity split assets before building read-only RomFS'
    assert all((a.game/'assets/bin/Data'/name).is_file() for name in ('globalgamemanagers.assets','sharedassets0.assets'))
    assert ('ultrakill_nx '+a.version).encode() in (a.game/'ultrakill_nx.nro').read_bytes()
    assert b'nxb_touch' in (a.game/'libil2cpp.so').read_bytes()
    if a.version in ('r83','r84','r85','r86','r87','r88','r89','r90','r91','r92','r93'):
        assert all((a.game/'assets/NxPort/lang'/f'{code}.tsv').is_file() for code in
                   ('ru','uk','de','fr','es','pt-BR','it','pl','tr','ja','ko','zh-Hans','zh-Hant')), 'Incomplete r83 languages'
        assert (a.game/'assets/NxPort/nx_fonts.bundle').is_file(), 'Missing r83 CJK font'
        assert (a.game/'licenses/NotoSansCJK-OFL.txt').is_file(), 'Missing CJK OFL license'
    boot=json.loads((a.game/'assets/NxPort/boot-content.json').read_text(encoding='utf-8'))
    assert boot['source']=='ported-game'
    from stage_full_game import BOOT_FILES,RUNTIME_FILES
    assert set(boot['files'])==set(BOOT_FILES+RUNTIME_FILES)
    assert all(digest(a.game/'assets/bin/Data'/name)==expected for name,expected in boot['files'].items()), 'Boot graph/metadata provenance mismatch'
    payload_bytes=sum(p.stat().st_size for p in a.game.rglob('*') if p.is_file())
    # The patched local packer releases RomFS temp after NCA writers close;
    # only NCA + NSP coexist. Stock packers still need space for all three.
    copies=2 if b'NX_RELEASE_TEMP_BEFORE_NSP' in a.hacbrewpack.read_bytes() else 3
    needed=payload_bytes*copies+512*1024*1024
    assert shutil.disk_usage(a.work.parent).free>=needed, f'Need {needed/1024**3:.1f} GiB free for packaging'
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
    display={'r52':'2.0.0-test16','r53':'2.0.0-test17','r54':'2.0.0-test18','r55':'2.0.0-test19','r56':'2.0.0-test20','r57':'2.0.0-test21','r58':'2.0.0-test22','r59':'2.0.0-test23','r60':'2.0.0-test24','r61':'2.0.0-test25','r62':'2.0.0-test26','r63':'2.0.0-test27','r64':'2.0.0-test28','r65':'2.0.0-test29','r66':'2.0.0-test30','r67':'2.0.0-test31','r68':'2.0.0-test32','r69':'2.0.0-test33','r70':'2.0.0-test34','r71':'2.0.0-test35','r72':'2.0.0-test36','r73':'2.0.0-test37','r74':'2.0.0-test38','r75':'2.0.0-test39','r76':'2.0.0-test40','r77':'2.0.0-test41','r78':'2.0.0-test42','r79':'2.0.0-test43','r80':'2.0.0-test44','r81':'2.0.0-test45','r82':'2.0.0-test46','r83':'2.0.0-test47','r84':'2.0.0-test48','r85':'2.0.0-test49','r86':'2.0.0-test50','r87':'2.0.0-test51','r88':'2.0.0-test52','r89':'2.0.0-test53','r90':'2.0.0-test54','r91':'2.0.0-test55','r92':'2.0.0-test56','r93':'2.0.0-test57'}[a.version]
    subprocess.run([str(a.nacptool),'--create','ULTRAKILL - NX','Dimasick-git',display,str(control/'control.nacp')],check=True)
    shutil.copy2(root/'forwarder/icon.jpg',control/'icon_AmericanEnglish.dat')
    manifest={'version':a.version,'title_id':'01005CE3B1A40000','storage':'title RomFS; SD saves/config/cache',
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
    report=verify(outputs[0],a.keys,expected_version=a.version)
    assert report['bytes']<0xffffffff,'Installer does not fit one FAT32 file'
    a.output.parent.mkdir(parents=True,exist_ok=True)
    temporary=a.output.with_suffix('.nsp.tmp');shutil.move(str(outputs[0]),str(temporary))
    assert digest(temporary)==report['sha256'];temporary.replace(a.output)
    report['file']=str(a.output.resolve())
    a.output.with_suffix('.verified.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps(report,indent=2),flush=True)
if __name__=='__main__':main()
