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
    for name in ('base','export','nro','native-elf','output','unity-log','il2cpp-log'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--fsr-bundles', required=True, type=Path)
    p.add_argument('--version',choices=tuple(f'r{n}' for n in range(52,94)),default='r52')
    p.add_argument('--language-dir',type=Path,help='Generated language TSV tables')
    p.add_argument('--font-bundle',type=Path,help='Android nx_fonts.bundle')
    p.add_argument('--font-license',type=Path,help='Upstream SIL OFL license for the CJK font')
    p.add_argument('--without-frame-generation',action='store_true')
    p.add_argument('--render-check-log',type=Path,help='Transactional MRT/camera restart GPU check log')
    p.add_argument('--scene-memory-log',type=Path,help='Unity scene cleanup lifecycle check log (r88+)')
    a=p.parse_args();assert not a.output.exists(),'Choose a new staging directory'
    assert '[nxbuild] Android: Succeeded' in a.unity_log.read_text(encoding='utf-8')
    assert re.search(r'Build succeeded with \d+ successful nodes and 0 failed',a.il2cpp_log.read_text(encoding='utf-8'))
    libs=a.export/'jniLibs/arm64-v8a';lib,sym=Elf(libs/'libil2cpp.so'),Elf(libs/'libil2cpp.sym.so')
    assert lib.build_id()==sym.build_id()
    names=sym.symbols();assert all(any(tag in n for n in names) for tag in ('NxTouchModule','NxSoftKeyboard','NxPortSettings','nx_gc_bridge_state'))
    if a.version=='r84':
        assert all(any(tag in n for n in names) for tag in
                   ('NxSettingsLabel_', 'NxPortSettings_FirstVisible_', 'NxLoc_Refresh_')), 'Stale pre-r84 IL2CPP library in Gradle export; copy the freshly compiled library and matching symbols first'
        assert b'NxSettingsLabel' in (a.export/'assets/bin/Data/Managed/Metadata/global-metadata.dat').read_bytes(), 'Stale pre-r84 code metadata'
    if a.version in ('r85','r86','r87','r88','r89','r90','r91','r92','r93'):
        assert all(any(tag in n for n in names) for tag in
                   ('NxPortSettings_AttachNative_', 'NxPortSettings_AttachPage_', 'NxPortSettings_UpdateCommunityLink_')), 'Stale pre-r85 settings library'
        assert '[nx-settings] PASS:' in a.unity_log.read_text(encoding='utf-8'), 'Settings checks missing'
    if a.version in ('r86','r87','r88','r89','r90','r91','r92','r93'):
        assert any('NxPortSettings_ConfigureCommunity_' in n for n in names), 'Stale pre-r86 community button library'
        assert '[nx-community] PASS:' in a.unity_log.read_text(encoding='utf-8'), 'Community row fitting check missing'
        assert any('NxLoc_AttachFallbacks_' in n for n in names), 'Late font fallback code missing'
        assert '[nx-text] PASS:' in a.unity_log.read_text(encoding='utf-8'), 'Death/close/font regression checks missing'
        assert '[nx-font] PASS:' in a.unity_log.read_text(encoding='utf-8'), 'Native font bundle validation missing'
        font_manifest = a.font_bundle.with_suffix('.bundle.manifest').read_text(encoding='utf-8').lower()
        assert all(name in font_manifest for name in ('vcr-osd-replayed.ttf', 'liberationsans.ttf', 'nxcjk.ttf')), 'Incomplete UI font bundle'
    if a.version in ('r87','r88','r89','r90','r91','r92','r93'):
        assert any('NxDeathTextBinding_Show_' in n for n in names), 'Localized death renderer missing'
        assert '[nx-death] PASS:' in a.unity_log.read_text(encoding='utf-8'), 'Localized death geometry check missing'
    if a.version in ('r88','r89','r90','r91','r92','r93'):
        assert any('NxSceneMemory_WrapSceneLoad_' in n for n in names), 'Scene cleanup library missing'
        assert '[nx-memory-order] PASS:' in a.unity_log.read_text(encoding='utf-8'), 'Scene cleanup ordering check missing'
        assert any('nouveau_nx_trim_memory' in name for name in Elf(a.native_elf).symbols()), 'Mesa trim missing'
        assert '// NX_SCENE_MEMORY' in (a.export/'Il2CppOutputProject/Source/il2cppOutput/Assembly-CSharp.cpp').read_text(encoding='utf-8'), 'Scene factory cleanup hook missing'
        memory_log = a.scene_memory_log or a.unity_log.with_name(f'unity-{a.version}-memory-isolated.log')
        assert '[nx-memory-scenes] PASS:' in memory_log.read_text(encoding='utf-8'), 'Real scene cleanup lifecycle check missing'
        assert b'nxb_memory_trim' in (libs/'libil2cpp.so').read_bytes(), 'Render-thread trim bridge missing'
    if a.version in ('r89','r90','r91','r92','r93'):
        assert any('// NX_RENDER_OWNER_TEARDOWN' in p.read_text(encoding='utf-8') for p in (a.export/'Il2CppOutputProject/Source/il2cppOutput').glob('Assembly-CSharp*.cpp')), 'Compositor teardown hook must be compiled before packaging'
        assert a.render_check_log and '[nx-render-restart] PASS:' in a.render_check_log.read_text(encoding='utf-8'), 'GPU restart check missing'
        assert all(any(tag in n for n in names) for tag in ('NxGameRenderTargets_SyncCameras_', 'NxBridge_RenderFaults_')), 'Render recovery library missing'
        assert any('nxb_render_faults' in name for name in Elf(a.native_elf).symbols()), 'GPU fault bridge missing'
        assert any('// NX_AUDIO_LISTENER_GUARD' in p.read_text(encoding='utf-8') for p in (a.export/'Il2CppOutputProject/Source/il2cppOutput').glob('Assembly-CSharp*.cpp')), 'Loading audio guard missing'
    if a.version in ('r92','r93'):
        assert all(any(tag in n for n in names) for tag in ('NxSceneMemory_UnloadScene_', 'NxGameRenderTargets_Detach_')), 'Restart completion/detach library missing'
        generated=a.export/'Il2CppOutputProject/Source/il2cppOutput'
        assert any('// NX_FPS_MENU_REQUEST' in p.read_text(encoding='utf-8') for p in generated.glob('Assembly-CSharp*.cpp')), 'Immediate FPS menu hook missing'
        assert any('// NX_RENDER_DETACH' in p.read_text(encoding='utf-8') for p in generated.glob('Assembly-CSharp*.cpp')), 'Camera detach must precede texture teardown'
        assert any('nxb_fps_request' in n for n in Elf(a.native_elf).symbols()), 'Immediate FPS export missing'
        assert 'five paused repeated-mission cleanup cycles' in a.scene_memory_log.read_text(encoding='utf-8'), 'Paused repeated restart test missing'
    assert any('NxPerformance_Awake' in n for n in names), 'One-shot visual profile missing'
    assert not any('NxPerformance_Update' in n for n in names), 'TEST10 per-frame settings search regression present'
    assert any('NxSpawnWarmup_Prewarm' in n for n in names), 'Enemy material prewarm missing'
    assert not any('NxSpawnWarmup_Update' in n for n in names), 'Enemy prewarm must not run per frame'
    assert any('NxRenderTransition_TakeReset' in n for n in names), 'Scene-settle recovery missing'
    assert any('NxFrameTimings_Update' in n for n in names), 'Unity engine timing capture missing'
    assert any('NxLoopTimings_OnEnable' in n for n in names), 'Release native-loop brackets missing'
    assert any('NxPhysicsQueries_BeginAimRead' in n for n in names), 'Local query synchronization missing'
    assert any('NxFsr_NativeComposition' in n for n in names), 'FSR OFF native composition path missing'
    generated=a.export/'Il2CppOutputProject/Source/il2cppOutput'
    assert '// NX_AIM_FILTER_BEFORE_OCCLUSION' in (generated/'Assembly-CSharp__2.cpp').read_text(encoding='utf-8'), 'Autoaim CPU optimization missing'
    assert '// NX_STAIN_TRANSFER_MISSING_OLD_PARENT' in (generated/'Assembly-CSharp__2.cpp').read_text(encoding='utf-8'), 'Missing-parent stain transfer repair missing'
    assert (generated/'Assembly-CSharp__2.cpp').read_text(encoding='utf-8').count('// NX_AIM_SYNC_BATCH')==2, 'Autoaim read scopes missing'
    aim=(generated/'Assembly-CSharp__2.cpp').read_text(encoding='utf-8')
    late=aim[aim.index('IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void CameraFrustumTargeter_LateUpdate_m'):]
    late=late[:late.index('// Method Definition Index:')]
    assert late.index('NxAimReadBatch nx_aim_read')>late.index('IL_0027:')>late.index('return;'), 'Empty autoaim LateUpdate still synchronizes physics'
    assert sum(p.read_text(encoding='utf-8-sig').count('// NX_CONTACTS_NO_ARRAY') for p in generated.glob('Assembly-CSharp*.cpp'))==5, 'Allocation-free contact readers missing'
    assert '// NX_STOPPED_GORE_SLEEP' in (generated/'Assembly-CSharp__13.cpp').read_text(encoding='utf-8'), 'Stopped gore sleep missing'
    assert all(n.encode() in (libs/'libil2cpp.so').read_bytes() for n in ('nxb_touch','nxb_config_set','nxb_fsr_applied'))
    assert b'nxb_scope_record' in (libs/'libil2cpp.so').read_bytes(), 'CPU timing instrumentation missing'
    assert b'nxb_bounds_site' in (libs/'libil2cpp.so').read_bytes(), 'Bounds diagnostics missing'
    assert any('Fsr3UpscalerContext' in name for name in names), 'FSR 3.1 backend missing'
    native=Elf(a.native_elf);native_id=bytes.fromhex(native.build_id())
    assert a.nro.read_bytes()[0x40:0x40+len(native_id)]==native_id, 'NRO/native symbol file mismatch'
    from audit_imports import audit
    imports=audit(a.native_elf,[libs/n for n in ('libmain.so','libunity.so','libil2cpp.so')])
    assert all(not r['unresolved'] for r in imports.values()),imports
    offsets,notes=derive_il2cpp(lib,sym);assert not notes,notes
    assert all('il2cpp.video_'+name in offsets for name in VIDEO_METHODS)
    a.output.mkdir(parents=True)
    # Preserve game bundles/videos and the original ported boot object graph.
    # Hard links are only used for immutable base assets; never edit them in place.
    for src in a.base.rglob('*'):
        if not src.is_file():continue
        rel=src.relative_to(a.base)
        if not (rel.as_posix().startswith(('assets/','licenses/'))):continue
        if a.without_frame_generation and rel.as_posix().startswith('assets/NxPort/framegen/'):continue
        if rel.as_posix().startswith('assets/bin/Data/'):continue
        dst=a.output/rel;dst.parent.mkdir(parents=True,exist_ok=True)
        try:os.link(src,dst)
        except OSError:shutil.copy2(src,dst)
    boot=overlay_runtime(a.base/'assets/bin/Data',a.export/'assets/bin/Data',a.output/'assets/bin/Data')
    join_unity_splits(a.output/'assets/bin/Data')
    boot_path=a.output/'assets/NxPort/boot-content.json'
    boot_temp=boot_path.with_suffix('.json.tmp')
    boot_temp.write_text(json.dumps({'source':'ported-game','files':boot},indent=2)+'\n',encoding='utf-8')
    boot_temp.replace(boot_path)
    if a.language_dir:
        for src in sorted(a.language_dir.glob('*.tsv')):
            dst=a.output/'assets/NxPort/lang'/src.name;dst.parent.mkdir(parents=True,exist_ok=True)
            temporary=dst.with_suffix('.tsv.tmp');shutil.copy2(src,temporary);temporary.replace(dst)
    if a.font_bundle or a.font_license:
        assert a.font_bundle and a.font_license, 'Supply both font bundle and OFL license'
        for src,dst in ((a.font_bundle,a.output/'assets/NxPort/nx_fonts.bundle'),
                        (a.font_license,a.output/'licenses/NotoSansCJK-OFL.txt')):
            dst.parent.mkdir(parents=True,exist_ok=True)
            temporary=dst.with_name(dst.name+'.nx-tmp');shutil.copy2(src,temporary);temporary.replace(dst)
    if a.version in ('r83','r84','r85','r86','r87','r88','r89','r90','r91','r92','r93'):
        assert all((a.output/'assets/NxPort/lang'/f'{code}.tsv').is_file() for code in
                   ('ru','uk','de','fr','es','pt-BR','it','pl','tr','ja','ko','zh-Hans','zh-Hant')), 'Incomplete r83 languages'
        assert (a.output/'assets/NxPort/nx_fonts.bundle').is_file(), 'Missing r83 CJK font'
        assert (a.output/'licenses/NotoSansCJK-OFL.txt').is_file(), 'Missing CJK OFL license'
    for name in ('nx_fsr.bundle','nx_fsr3.bundle'):
        src=a.fsr_bundles/name; assert src.is_file() and src.stat().st_size>1000, 'Missing upscaler bundle: '+name
        dst=a.output/'assets/NxPort'/name
        temporary=dst.with_name(dst.name+'.nx-tmp');shutil.copy2(src,temporary);temporary.replace(dst)
    # Frame generation uses native DKSH, not Unity shader translation. Replace
    # inherited hard links atomically so an older build stays immutable.
    if not a.without_frame_generation:
        fg=a.export/'assets/NxPort/framegen'
        assert fg.is_dir(), 'Missing staged deko frame-generation shaders'
        manifest=json.loads((fg/'manifest.json').read_text(encoding='utf-8'))
        assert len(manifest['passes'])==18
        for src in fg.iterdir():
            if not src.is_file(): continue
            dst=a.output/'assets/NxPort/framegen'/src.name
            dst.parent.mkdir(parents=True,exist_ok=True)
            temporary=dst.with_name(dst.name+'.nx-tmp');shutil.copy2(src,temporary);temporary.replace(dst)
    else:
        assert not any(name.startswith(('dkDevice','dkQueue','ffxFrameInterpolation','ffxOpticalflow')) for name in native.symbols()), 'Lean stage has deko FG code'
        assert any('NxFsr_EnsureMaterial' in name for name in names), 'Lazy FSR shader loading missing'
    license_dir=Path(__file__).resolve().parents[1]/'unity/Packages/fidelityfx.fsr'
    for src,name in ((license_dir/'LICENSE.md','FSR3Unity-MIT.txt'),
                     (license_dir/'Shaders/shaders/fsr3upscaler/ffx_fsr3upscaler_callbacks_hlsl.h','FSR3-AMD-source-notice.txt')):
        dst=a.output/'licenses'/name;dst.parent.mkdir(exist_ok=True);shutil.copy2(src,dst)
    shutil.copy2(Path(__file__).resolve().parents[1]/'loader/source/vendor/sdl2285/LICENSE.txt',a.output/'licenses/SDL2-NX-audio.txt')
    for name in ('libmain.so','libunity.so','libil2cpp.so'):shutil.copy2(libs/name,a.output/name)
    shutil.copy2(a.nro,a.output/'ultrakill_nx.nro')
    old=(a.base/'offsets.ini').read_text(encoding='utf-8').splitlines()
    lines=[line for line in old if not line.startswith(('il2cpp.','# libil2cpp'))]
    lines+=[f'# libil2cpp {a.version} build-id {lib.build_id()}']+[f'{key} = 0x{value:x}' for key,value in sorted(offsets.items())]
    (a.output/'offsets.ini').write_bytes(('\n'.join(lines)+'\n').encode())
    assert ('ultrakill_nx '+a.version).encode() in a.nro.read_bytes()
    assert b'UnityEngine.Time::set_fixedDeltaTime' not in a.nro.read_bytes()
    report={'version':a.version,'frame_generation_compiled':not a.without_frame_generation,'build_id':lib.build_id(),'imports':imports,'files':{f.relative_to(a.output).as_posix():{'bytes':f.stat().st_size,'sha256':sha(f)} for f in sorted(a.output.rglob('*')) if f.is_file()}}
    a.output.with_suffix('.verified.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps({'version':a.version,'build_id':lib.build_id(),'files':len(report['files']),'bytes':sum(f['bytes'] for f in report['files'].values())},indent=2))
if __name__=='__main__':main()
