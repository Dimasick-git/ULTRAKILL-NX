"""Build unchanged AMD host schedulers, with checked platform compatibility."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT/'experimental/framegen/vendor/fidelityfx-host-1.1.3'
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cxx',required=True);ap.add_argument('--ar',required=True);ap.add_argument('--output',required=True,type=Path);ap.add_argument('--switch',action='store_true');a=ap.parse_args()
 provenance=json.loads((HOST/'upstream.json').read_text())
 for rel,digest in provenance['files'].items():assert hashlib.sha256((HOST/rel).read_bytes()).hexdigest()==digest,rel
 a.output.mkdir(parents=True,exist_ok=True)
 common=['-std=c++17','-O2','-g','-fno-exceptions','-fno-rtti','-DFFX_GCC','-DNDEBUG',
 '-include',str(ROOT/'experimental/framegen/deko/host_compat.h'),'-I'+str(HOST/'include'),'-I'+str(HOST/'include/FidelityFX/gpu'),'-I'+str(HOST/'src/shared')]
 if a.switch:common+=['-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-D__SWITCH__']
 objects=[]
 for effect in ('opticalflow','frameinterpolation'):
  src=HOST/f'src/components/{effect}/ffx_{effect}.cpp';obj=a.output/(effect+'.o')
  subprocess.run([a.cxx,*common,'-c',str(src),'-o',str(obj)],check=True);objects.append(obj)
 src=HOST/'src/shared/ffx_object_management.cpp';obj=a.output/'object_management.o'
 subprocess.run([a.cxx,*common,'-c',str(src),'-o',str(obj)],check=True);objects.append(obj)
 library=a.output/'libffx_fg_host.a';subprocess.run([a.ar,'rcs',str(library),*map(str,objects)],check=True)
 report={'sdk_revision':provenance['commit'],'library':str(library),'switch':a.switch,'sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'note':'CPU scheduler only; GPU backend/presentation not provided by this archive'}
 (a.output/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
