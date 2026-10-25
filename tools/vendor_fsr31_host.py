"""Vendor the pinned AMD CPU scheduler and its header dependency closure."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
REV='54fbaafdc34716811751bea5032700e78f5a0f33'
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sdk',required=True,type=Path);a=ap.parse_args()
 assert subprocess.check_output(['git','-C',str(a.sdk),'rev-parse','HEAD'],text=True).strip()==REV
 src=a.sdk/'sdk';dest=ROOT/'experimental/framegen/vendor/fidelityfx-host-1.1.3'
 pending=[src/'src/components'/effect/f'ffx_{effect}{suffix}' for effect in ('opticalflow','frameinterpolation') for suffix in ('.cpp','_private.h')]
 pending += [src/'src/shared/ffx_object_management.h',src/'src/shared/ffx_object_management.cpp']
 found={}
 while pending:
  file=pending.pop();rel=file.relative_to(src).as_posix()
  if rel in found:continue
  assert file.is_file(),file
  body=file.read_bytes();found[rel]=hashlib.sha256(body).hexdigest()
  target=dest/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
  for include in re.findall(rb'^\s*#\s*include\s*[<"]([^>"]+)[>"]',body,re.M):
   name=include.decode()
   options=[file.parent/name,src/'include'/name,src/'include/FidelityFX/gpu'/name,src/'src/shared'/name]
   match=next((p for p in options if p.is_file()),None)
   if match:pending.append(match.resolve())
   elif name.startswith(('FidelityFX/','ffx_')):raise ValueError('Missing SDK dependency '+name)
 shutil.copy2(a.sdk/'LICENSE.txt',dest/'LICENSE.txt')
 report={'repository':'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK','commit':REV,'tag':'v1.1.3','files':found}
 (dest/'upstream.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print(f'Vendored {len(found)} unchanged AMD CPU scheduler files at {REV}')
if __name__=='__main__':main()
