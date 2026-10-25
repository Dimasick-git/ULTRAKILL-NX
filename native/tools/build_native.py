"""Build current native app after its actual dependency libraries.

This produces an internal WIP NRO, never a release NSP. It does not write to SD.
"""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess,sys

def msys(path):
 value=Path(path).resolve().as_posix()
 if len(value)>2 and value[1]==':':return '/'+value[0].lower()+value[2:]
 return value

def main():
 p=argparse.ArgumentParser();p.add_argument('--jolt',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cmake',type=Path,required=True);p.add_argument('--ninja',type=Path,required=True);p.add_argument('--bash',type=Path,required=True);p.add_argument('--devkitpro',type=Path,default=Path('C:/devkitPro'));p.add_argument('--jobs',type=int,default=3);args=p.parse_args()
 root=Path(__file__).resolve().parents[1];args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
 for name in ('migration/level1-2.nxrender','migration/level1-2.nxjoint','migration/level1-2.nxactor','migration/level1-2.nxmat','migration/level1-2.nxphys','migration/level1-2.nxgraph'):
  if not (args.output/name).is_file():raise FileNotFoundError('Generate native resources before building: '+name)
 subprocess.run([sys.executable,str(root/'tools/build_physics.py'),'--jolt',str(args.jolt),'--output',str(args.output/'physics-switch'),'--cmake',str(args.cmake),'--ninja',str(args.ninja),'--switch','--devkitpro',str(args.devkitpro),'--jobs',str(args.jobs)],check=True)
 sdk=msys(args.devkitpro)
 command=['env','DEVKITPRO='+sdk,'DEVKITA64='+sdk+'/devkitA64','PATH='+sdk+'/devkitA64/bin:'+sdk+'/tools/bin:/usr/bin:/bin','/usr/bin/make','-C',msys(args.output),'-f',msys(root/'Makefile'),'SRCROOT='+msys(root),'-j'+str(args.jobs),'all']
 subprocess.run([str(args.bash),'-lc',shlex.join(command)],check=True)
 artifact=args.output/'ultrakill_nx_native.nro'
 manifest={'version':'native-dev13','file':str(artifact),'bytes':artifact.stat().st_size,'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest(),'render_pack_sha256':hashlib.sha256((args.output/'romfs/level1-2.nxrender').read_bytes()).hexdigest(),'material_pack_sha256':hashlib.sha256((args.output/'romfs/level1-2.nxmat').read_bytes()).hexdigest(),'joint_pack_sha256':hashlib.sha256((args.output/'romfs/level1-2.nxjoint').read_bytes()).hexdigest(),'actor_pack_sha256':hashlib.sha256((args.output/'romfs/level1-2.nxactor').read_bytes()).hexdigest(),'physics_pack_sha256':hashlib.sha256((args.output/'romfs/level1-2.nxphys').read_bytes()).hexdigest(),'graph_pack_sha256':hashlib.sha256((args.output/'romfs/level1-2.nxgraph').read_bytes()).hexdigest(),'revolver_control_library_built':True,'revolver_runtime_connected':False,'hardware_verified':False,'complete_game':False,'release_nsp':False,'scope':'owned local-geometry deko3d renderer with hierarchy-driven visibility/transforms, ordinary native player/physics and scene graph; original base/emissive texture resources connected; compound rigid/kinematic actor primitives with hierarchy bindings connected; native joint configuration/lifecycle and measured actor force/mass/gravity primitives connected; full ragdoll verification, lighting/material effects, full scene/controller/portal/effect lifecycle, weapons, enemies, audio, menus and scene scripts incomplete','sd_written':False,'published':False}
 (args.output/'native-build.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))

if __name__=='__main__':main()
