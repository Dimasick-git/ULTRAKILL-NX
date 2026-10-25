"""Prove an optional SDK binding is dead by identical compiled DKSH bytes."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--shaders',required=True,type=Path);ap.add_argument('--uam',required=True);a=ap.parse_args()
 name='ffx_frameinterpolation_optical_flow_vector_field_pass';source=(a.shaders/(name+'.glsl')).read_text()
 # AMD computes fConfidenceFactor but never consumes it in SDK 1.1.3.
 # Verify compiler elimination instead of binding dummy data to a missing input.
 changed,n=re.subn(r'float LoadOpticalFlowConfidence\(in ivec2 iPxPos\)\s*\{[^{}]*\}',
                    'float LoadOpticalFlowConfidence(in ivec2 iPxPos) { return 0.0f; }',source)
 assert n==1
 changed,n=re.subn(r'layout\s*\(binding\s*=\s*\d+\)\s*uniform usampler2D r_optical_flow_confidence\s*;','',changed);assert n==1
 with tempfile.TemporaryDirectory() as temporary:
  src=Path(temporary)/'proof.glsl';out=Path(temporary)/'proof.dksh';src.write_text(changed)
  subprocess.run([a.uam,'-s','comp','-o',str(out),str(src)],check=True)
  before=(a.shaders/(name+'.dksh')).read_bytes();after=out.read_bytes()
  assert before==after,'Binding removal changes the GPU program; cannot omit it'
 report={'pass':name,'sha256':hashlib.sha256(before).hexdigest(),'omitted_bindings':['r_optical_flow_confidence'],'proof':'DKSH byte-identical after replacing confidence load with constant and removing its sampler'}
 (a.shaders/'unused-binding-proof.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS: unused confidence binding proven by byte-identical DKSH')
if __name__=='__main__':main()
