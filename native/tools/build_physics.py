"""Build/check the native physics adapter, with strict exit-code handling."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys

def main():
 p=argparse.ArgumentParser();p.add_argument('--jolt',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cmake',type=Path,required=True);p.add_argument('--ninja',type=Path,required=True);p.add_argument('--host-compiler',type=Path);p.add_argument('--switch',action='store_true');p.add_argument('--devkitpro',type=Path,default=Path('C:/devkitPro'));p.add_argument('--pack',type=Path);p.add_argument('--graph',type=Path);p.add_argument('--jobs',type=int,default=3)
 p.add_argument('--render',type=Path);p.add_argument('--materials',type=Path);p.add_argument('--actors',type=Path);p.add_argument('--joints',type=Path);p.add_argument('--force-reference',type=Path);p.add_argument('--revolver-reference',type=Path);args=p.parse_args()
 if args.render and not args.graph:p.error('--render requires --graph')
 if args.materials and not args.render:p.error('--materials requires --render')
 if args.actors and (not args.pack or not args.graph):p.error('--actors requires --pack and --graph')
 if args.joints and not args.actors:p.error('--joints requires --actors')
 if not args.switch and not args.host_compiler:p.error('--host-compiler is required for host checks')
 root=Path(__file__).resolve().parents[1];args.output.mkdir(parents=True,exist_ok=True)
 (args.output/'physics-validation.json').write_text(json.dumps({'status':'running','hardware_verified':False,'gameplay_complete':False})+'\n')
 environment=os.environ.copy();environment['DEVKITPRO']=args.devkitpro.as_posix()
 subprocess.run([sys.executable,str(root/'tools/patch_jolt_switch.py'),str(args.jolt)],check=True,env=environment)
 configure=[str(args.cmake),'-S',str(root/'physics'),'-B',str(args.output),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+args.ninja.as_posix(),'-DJOLT_SOURCE='+args.jolt.as_posix()]
 if args.switch:configure+=['-DCMAKE_TOOLCHAIN_FILE='+(root/'physics/switch-toolchain.cmake').as_posix(),'-DDEVKITPRO='+args.devkitpro.as_posix(),'-DCMAKE_BUILD_TYPE=Distribution']
 else:configure+=['-DCMAKE_CXX_COMPILER='+args.host_compiler.as_posix(),'-DCMAKE_BUILD_TYPE=Release']
 subprocess.run(configure,check=True,env=environment)
 subprocess.run([str(args.cmake),'--build',str(args.output),'--parallel',str(args.jobs)],check=True,env=environment)
 test_output='not run on hardware'
 if not args.switch:
  command=[str(args.output/('nx_physics_test.exe' if os.name=='nt' else 'nx_physics_test'))]
  if args.pack:command.append(str(args.pack))
  result=subprocess.run(command,text=True,capture_output=True);test_output=result.stdout+result.stderr;print(test_output,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  for name,extra in [('nx_ground_test',[]),('nx_jump_test',[]),('nx_dash_test',[]),('nx_crouch_test',[]),('nx_world_test',[str(args.graph)] if args.graph else []),('nx_activation_test',[]),('nx_session_test',([str(args.pack)]+([str(args.graph)] if args.graph else [])+([str(args.actors)] if args.actors else [])+([str(args.joints)] if args.joints else [])) if args.pack else [])]:
   if name=='nx_session_test' and not args.pack:continue
   result=subprocess.run([str(args.output/(name+('.exe' if os.name=='nt' else ''))),*extra],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  if args.render:
   result=subprocess.run([str(args.output/('nx_render_test.exe' if os.name=='nt' else 'nx_render_test')),str(args.render),str(args.graph)],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  if args.materials:
   result=subprocess.run([str(args.output/('nx_material_test.exe' if os.name=='nt' else 'nx_material_test')),str(args.materials),str(args.render)],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  if args.actors:
   result=subprocess.run([str(args.output/('nx_actor_test.exe' if os.name=='nt' else 'nx_actor_test')),str(args.actors),str(args.pack),str(args.graph)],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  if args.joints:
   result=subprocess.run([str(args.output/('nx_joint_test.exe' if os.name=='nt' else 'nx_joint_test')),str(args.joints),str(args.actors),str(args.graph)],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  result=subprocess.run([str(args.output/('nx_force_test.exe' if os.name=='nt' else 'nx_force_test'))],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  if args.force_reference:
   result=subprocess.run([sys.executable,str(root/'tools/check_force_oracle.py'),'--reference',str(args.force_reference),'--runner',str(args.output/('nx_force_test.exe' if os.name=='nt' else 'nx_force_test')),'--output',str(args.output/'force-reference')],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  if args.revolver_reference:
   result=subprocess.run([sys.executable,str(root/'tools/check_revolver.py'),'--cpp',str(args.revolver_reference),'--compiler',str(args.host_compiler),'--output',str(args.output/'revolver-reference')],text=True,capture_output=True);test_output+=result.stdout+result.stderr;print(result.stdout+result.stderr,end='');(args.output/'physics-test.log').write_text(test_output);result.check_returncode()
  (args.output/'physics-test.log').write_text(test_output)
 artifacts=[args.output/'libJolt.a',args.output/'libNxPhysics.a',args.output/'libNxGameplay.a']
 if args.switch:artifacts.append(args.output/'nx_physics_link.elf')
 report={'status':'switch_built' if args.switch else 'host_passed','target':'Switch ARM64' if args.switch else 'host','jolt_revision':subprocess.check_output(['git','-C',str(args.jolt),'rev-parse','HEAD'],text=True).strip(),'dependency_files':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in (args.jolt/'Jolt/Core/Core.h',args.jolt/'Jolt/Core/Memory.cpp')},'artifacts':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in artifacts},'test_output':test_output,'actual_scene_test':bool(args.pack and not args.switch),'hardware_verified':False,'gameplay_complete':False}
 (args.output/'physics-validation.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
