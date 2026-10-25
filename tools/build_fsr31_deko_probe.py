"""Build an isolated Switch GPU conformance probe; never deploy or alter the game."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess
from build_fsr31_deko import convert,PYRAMID,ROOT,VENDOR

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--shaders',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);ap.add_argument('--devkitpro',type=Path,default=Path('C:/devkitPro'));a=ap.parse_args()
 assert not a.output.exists(),'Choose a fresh probe directory'
 manifest=json.loads((a.shaders/'manifest.json').read_text());assert len(manifest['passes'])==18 and all(p['compiled'] for p in manifest['passes'])
 a.output.mkdir(parents=True);rom=a.output/'romfs/shaders';rom.mkdir(parents=True)
 for item in manifest['passes']:
  p=a.shaders/(item['pass']+'.dksh');assert hashlib.sha256(p.read_bytes()).hexdigest()==item['sha256'];shutil.copy2(p,rom/p.name)
 compat=(ROOT/'experimental/framegen/deko/subgroups.glsl').read_text(encoding='utf-8')
 for name in ('subgroup_probe','pyramid_probe','texture_probe','image_probe'):
  code=(ROOT/f'experimental/framegen/probe/{name}.glsl').read_text(encoding='utf-8')
  code,bindings=convert((PYRAMID if name=='pyramid_probe' else '')+code)
  # convert() does not inject another pyramid: this probe directly calls its adapter.
  src=a.output/(name+'.glsl');src.write_text('#version 460\n#extension GL_ARB_shader_ballot : require\n#extension GL_ARB_gpu_shader_int64 : require\n'+compat+'\n'+code,encoding='utf-8')
  subprocess.run([str(a.devkitpro/'tools/bin/uam.exe'),'-s','comp','-o',str(rom/(name+'.dksh')),str(src)],check=True)
  if name=='pyramid_probe':pyramid_bindings=bindings
 def slot(items,name,kind):return next(x['slot'] for x in items if x['name']==name and x['kind']==kind)
 luma=next(x for x in manifest['passes'] if x['pass']=='ffx_opticalflow_prepare_luma_pass')['resources']
 defines={'LUMA_INPUT_SLOT':slot(luma,'r_input_color','texture'),'LUMA_OUTPUT_SLOT':slot(luma,'rw_optical_flow_input','image'),'LUMA_CONSTANT_SLOT':slot(luma,'cbOF','uniform'),
 'PYRAMID_STORAGE_SLOT':slot(pyramid_bindings,'nxPyramid','storage'),'PYRAMID_LAYOUT_SLOT':slot(pyramid_bindings,'nxPyramidLayout','uniform'),'PYRAMID_RESULT_SLOT':slot(pyramid_bindings,'results','storage')}
 expected=[]
 for m in range(13):
  blob=struct.pack('<4e',float(m),m+0.5,-float(m),1.0);expected.append(struct.unpack('<2I',blob))
 header='\n'.join(f'#define {n} {v}' for n,v in defines.items())+'\nstatic const uint32_t pyramid_expected[13][2]={'+','.join('{'+','.join(hex(x)+'u' for x in row)+'}' for row in expected)+'};\n'
 (a.output/'probe_layout.h').write_text(header)
 for p in VENDOR.glob('LICENSE*'):shutil.copy2(p,a.output/'romfs'/p.name)
 shutil.copy2(VENDOR/'upstream.json',a.output/'romfs/upstream.json')
 shutil.copy2(a.shaders/'manifest.json',a.output/'romfs/shader-manifest.json')
 source=ROOT/'experimental/framegen/probe/main.cpp';shutil.copy2(source,a.output/'main.cpp')
 shutil.copy2(ROOT/'experimental/framegen/probe/sdk_gpu_probe.h',a.output/'sdk_gpu_probe.h')
 for name in ('nx_fg_interop.cpp','nx_fg_interop.h'):shutil.copy2(ROOT/'loader/source'/name,a.output/name)
 for name in ('host_compat.h','bindings_sdr.h','pyramid_layout.h','owned_memory.h','sdk_backend.h','sdk_backend.cpp','gpu_backend.h','gpu_backend.cpp'):
  shutil.copy2(ROOT/'experimental/framegen/deko'/name,a.output/name)
 host=ROOT/'experimental/framegen/vendor/fidelityfx-host-1.1.3'
 hostlib=a.output/'sdk-host/libffx_fg_host.a'
 subprocess.run(['python','-X','utf8',str(ROOT/'tools/build_fsr31_host.py'),'--cxx',str(a.devkitpro/'devkitA64/bin/aarch64-none-elf-g++.exe'),'--ar',str(a.devkitpro/'devkitA64/bin/aarch64-none-elf-ar.exe'),'--switch','--output',str(hostlib.parent)],check=True)
 env=dict(os.environ);env['DEVKITPRO']=a.devkitpro.as_posix();env['PATH']=str(a.devkitpro/'devkitA64/bin')+os.pathsep+env['PATH']
 elf=a.output/'fsr31-deko-probe.elf';nro=elf.with_suffix('.nro');nacp=elf.with_suffix('.nacp');romfs=a.output/'probe.romfs'
 subprocess.run([str(a.devkitpro/'devkitA64/bin/aarch64-none-elf-g++.exe'),'-specs='+str(a.devkitpro/'libnx/switch.specs'),'-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-O2','-g','-ffunction-sections','-std=gnu++17','-fno-exceptions','-fno-rtti','-Wall','-Wextra','-D__SWITCH__',
 '-I'+str(a.devkitpro/'portlibs/switch/include'),'-I'+str(a.devkitpro/'libnx/include'),'-I'+str(a.output),'-I'+str(host/'include'),str(a.output/'main.cpp'),str(a.output/'nx_fg_interop.cpp'),str(a.output/'sdk_backend.cpp'),str(a.output/'gpu_backend.cpp'),str(hostlib),'-L'+str(a.devkitpro/'portlibs/switch/lib'),'-L'+str(a.devkitpro/'libnx/lib'),'-Wl,-Map,'+str(elf.with_suffix('.map')),'-lEGL','-lGLESv2','-lglapi','-ldrm_nouveau','-lz','-ldeko3dd','-lnx','-lm','-o',str(elf)],check=True,env=env)
 subprocess.run([str(a.devkitpro/'tools/bin/build_romfs.exe'),str(a.output/'romfs'),str(romfs)],check=True)
 subprocess.run([str(a.devkitpro/'tools/bin/nacptool.exe'),'--create','FSR 3.1 deko GPU probe','Dimasick-git','0.1',str(nacp)],check=True)
 subprocess.run([str(a.devkitpro/'tools/bin/elf2nro.exe'),str(elf),str(nro),'--nacp='+str(nacp),'--romfs='+str(romfs)],check=True)
 report={'file':str(nro),'bytes':nro.stat().st_size,'sha256':hashlib.sha256(nro.read_bytes()).hexdigest(),'hardware_verified':False,'game_integrated':False,'sdk':manifest['upstream'],'tests':['AMD prepare_luma GPU vs CPU','subgroup full/partial warp reductions','13 mip packed RGBA16F read/write with guards','RGBA8 upload/download roundtrip','minimal sampled texture fetch vs CPU','minimal R8 storage image and guarded readback','GLES persistent PBO -> deko AMD luma -> GLES','full SDK OF/FI GPU graph: static RGB reference + moving midpoint'],'fence_timeout_ns':2000000000,'version':9,'transfer_strides':'zero: deko derives tightly packed byte strides from format','shared_source_sha256':{name:hashlib.sha256((a.output/name).read_bytes()).hexdigest() for name in ('nx_fg_interop.cpp','nx_fg_interop.h','sdk_backend.cpp','gpu_backend.cpp')},'sdk_host_sha256':hashlib.sha256(hostlib.read_bytes()).hexdigest()}
 (a.output/'probe-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
