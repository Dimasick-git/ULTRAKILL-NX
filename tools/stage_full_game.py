"""Combine original game bundles with a verified fresh Unity/IL2CPP export."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil
from derive_offsets import Elf,derive_il2cpp,VIDEO_METHODS

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

BOOT_FILES=('globalgamemanagers','globalgamemanagers.assets','globalgamemanagers.assets.resS',
            'level0','level0.resS','resources.assets','resources.assets.resS',
            'sharedassets0.assets','sharedassets0.assets.resS')
RUNTIME_FILES=('Managed/Metadata/global-metadata.dat','ScriptingAssemblies.json','RuntimeInitializeOnLoads.json')

def overlay_runtime(base_data,export_data,output_data):
    """Keep the ported game's object/reference graph; replace code metadata only.

    The editor Bootstrap is a code-generation scaffold. Its level0 and resources
    are not the game: replacing them loses SceneHelper.loadingBlocker and other
    serialized references, leaving the installed title on a black screen.
    Copy the complete baseline Data tree, including hashed dependency files.
    Atomic replacement also avoids modifying hard-linked release files.
    """
    assert all((base_data/name).is_file() for name in BOOT_FILES),'Missing ported game boot data'
    assert (base_data/'level0').read_bytes()!=(export_data/'level0').read_bytes(), 'Editor scaffold cannot be the game baseline'
    shutil.copytree(base_data,output_data)
    for name in RUNTIME_FILES:
        src=export_data/name;assert src.is_file(),name
        dst=output_data/name;dst.parent.mkdir(parents=True,exist_ok=True)
        temporary=dst.with_name(dst.name+'.nx-tmp');shutil.copy2(src,temporary);temporary.replace(dst)
    for name in BOOT_FILES:
        assert sha(base_data/name)==sha(output_data/name), 'Game boot graph changed: '+name
    return {name:sha(output_data/name) for name in BOOT_FILES+RUNTIME_FILES}
def join_unity_splits(data):
    """RomFS is read-only: complete Unity's split files before installation."""
    for first in sorted(data.rglob('*.split0')):
        base=first.with_name(first.name[:-7])
        parts=sorted(first.parent.glob(base.name+'.split*'),key=lambda p:int(p.name.rsplit('.split',1)[1]))
        assert [p.name for p in parts]==[base.name+f'.split{i}' for i in range(len(parts))], 'Missing Unity split: '+str(base)
        header=first.read_bytes()[:48];version=int.from_bytes(header[8:12],'big')
        assert 22<=version<=64
        declared=int.from_bytes(header[24:32],'big')
        assert declared==sum(p.stat().st_size for p in parts), 'Truncated Unity file: '+str(base)
        temporary=base.with_name(base.name+'.nx-tmp')
        with temporary.open('wb') as out:
            for part in parts:
                with part.open('rb') as source:shutil.copyfileobj(source,out)
        assert temporary.stat().st_size==declared
        temporary.replace(base)
        for part in parts:part.unlink()
def main():
    p=argparse.ArgumentParser()
    for name in ('base','export','nro','output','unity-log','il2cpp-log'):p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args();assert not a.output.exists(),'Choose a new staging directory'
    assert '[nxbuild] Android: Succeeded' in a.unity_log.read_text(encoding='utf-8')
    assert re.search(r'Build succeeded with \d+ successful nodes and 0 failed',a.il2cpp_log.read_text(encoding='utf-8'))
    libs=a.export/'jniLibs/arm64-v8a';lib,sym=Elf(libs/'libil2cpp.so'),Elf(libs/'libil2cpp.sym.so')
    assert lib.build_id()==sym.build_id()
    names=sym.symbols();assert all(any(tag in n for n in names) for tag in ('NxTouchModule','NxSoftKeyboard','NxPortSettings','nx_gc_bridge_state'))
    assert all(n.encode() in (libs/'libil2cpp.so').read_bytes() for n in ('nxb_touch','nxb_config_set','nxb_fsr_applied'))
    offsets,notes=derive_il2cpp(lib,sym);assert not notes,notes
    assert all('il2cpp.video_'+name in offsets for name in VIDEO_METHODS)
    a.output.mkdir(parents=True)
    # Preserve game bundles/videos and the original ported boot object graph.
    # Hard links are only used for immutable base assets; never edit them in place.
    for src in a.base.rglob('*'):
        if not src.is_file():continue
        rel=src.relative_to(a.base)
        if not (rel.as_posix().startswith(('assets/','licenses/'))):continue
        if rel.as_posix().startswith('assets/bin/Data/'):continue
        dst=a.output/rel;dst.parent.mkdir(parents=True,exist_ok=True)
        try:os.link(src,dst)
        except OSError:shutil.copy2(src,dst)
    boot=overlay_runtime(a.base/'assets/bin/Data',a.export/'assets/bin/Data',a.output/'assets/bin/Data')
    join_unity_splits(a.output/'assets/bin/Data')
    (a.output/'assets/NxPort/boot-content.json').write_text(json.dumps({'source':'ported-game','files':boot},indent=2)+'\n',encoding='utf-8')
    for name in ('libmain.so','libunity.so','libil2cpp.so'):shutil.copy2(libs/name,a.output/name)
    shutil.copy2(a.nro,a.output/'ultrakill_nx.nro')
    old=(a.base/'offsets.ini').read_text(encoding='utf-8').splitlines()
    lines=[line for line in old if not line.startswith(('il2cpp.','# libil2cpp'))]
    lines+=[f'# libil2cpp r35 build-id {lib.build_id()}']+[f'{key} = 0x{value:x}' for key,value in sorted(offsets.items())]
    (a.output/'offsets.ini').write_bytes(('\n'.join(lines)+'\n').encode())
    assert b'ultrakill_nx r35' in a.nro.read_bytes()
    assert b'UnityEngine.Time::set_fixedDeltaTime' not in a.nro.read_bytes()
    report={'version':'r35','build_id':lib.build_id(),'files':{f.relative_to(a.output).as_posix():{'bytes':f.stat().st_size,'sha256':sha(f)} for f in sorted(a.output.rglob('*')) if f.is_file()}}
    a.output.with_suffix('.verified.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps({'version':'r35','build_id':lib.build_id(),'files':len(report['files']),'bytes':sum(f['bytes'] for f in report['files'].values())},indent=2))
if __name__=='__main__':main()
