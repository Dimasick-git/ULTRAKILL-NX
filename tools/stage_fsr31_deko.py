"""Verify/copy native FG shaders and emit matching SDK/deko binding metadata."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,struct
ROOT=Path(__file__).resolve().parents[1]
OF=['prepare_luma_pass','compute_luminance_pyramid_pass','generate_scd_histogram_pass','compute_scd_divergence_pass','compute_optical_flow_advanced_pass_v5','filter_optical_flow_pass_v5','scale_optical_flow_advanced_pass_v5']
FI=['reconstruct_and_dilate_pass','setup_pass','reconstruct_previous_depth_pass','game_motion_vector_field_pass','optical_flow_vector_field_pass','disocclusion_mask_pass','pass','compute_inpainting_pyramid_pass','inpainting_pass','compute_game_vector_field_inpainting_pyramid_pass','debug_view_pass']
def stage(source,output,header=None):
 report=json.loads((source/'manifest.json').read_text());assert report['upstream']['commit']=='54fbaafdc34716811751bea5032700e78f5a0f33'
 assert not report['profile']['half'] and not report['profile']['hdr']
 passes={p['pass']:p for p in report['passes']};assert len(passes)==18
 proof=json.loads((source/'unused-binding-proof.json').read_text()) if (source/'unused-binding-proof.json').exists() else None
 omitted={}
 if proof:
  assert passes[proof['pass']]['sha256']==proof['sha256']
  assert proof['omitted_bindings']==['r_optical_flow_confidence']
  omitted[proof['pass']]=set(proof['omitted_bindings'])
 ordered=[(1,i,'ffx_opticalflow_'+n) for i,n in enumerate(OF)]+[(2,i,'ffx_frameinterpolation_'+n) for i,n in enumerate(FI)]
 lines=['#pragma once','struct NxFfxBinding { const char* name; const char* kind; unsigned slot; const char* sampler; };',
 'struct NxFfxPass { unsigned effect,pass; const char* file; const NxFfxBinding* bindings; unsigned count; bool packedPyramid; };']
 output.mkdir(parents=True,exist_ok=True)
 for effect,number,name in ordered:
  item=passes[name];blob=(source/(name+'.dksh')).read_bytes();assert item['compiled'] and hashlib.sha256(blob).hexdigest()==item['sha256']
  magic,head,control,size,programs,count=struct.unpack_from('<6I',blob)
  assert magic==0x48534b44 and head==24 and control+size==len(blob) and count==1
  info=struct.unpack_from('<16I',blob,programs);assert info[0]==5 and list(info[6:9])==item['workgroup'] and info[5]<=8192
  # Copy by replacement, safe even if output is a hard-linked staging tree.
  dst=output/(name+'.dksh');temp=dst.with_suffix('.dksh.tmp');temp.write_bytes(blob);temp.replace(dst)
  rows=[]
  for r in item['resources']:
   if r['name'] in omitted.get(name,set()):continue
   assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',r['name'])
   rows.append('{'+','.join([json.dumps(r['name']),json.dumps(r['kind']),str(r['slot']),json.dumps(r.get('sampler') or '')])+'}')
  lines.append(f'static const NxFfxBinding nx_ffx_{effect}_{number}[]={{'+','.join(rows)+'};')
 lines.append('static const NxFfxPass nx_ffx_passes[]={')
 for effect,number,name in ordered:
  item=passes[name];pyramid=any(r['name']=='nxPyramid' for r in item['resources'])
  binding_count=sum(r['name'] not in omitted.get(name,set()) for r in item['resources'])
  lines.append('{'+f'{effect},{number},"{name}",nx_ffx_{effect}_{number},{binding_count},'+str(pyramid).lower()+'},')
 lines.append('};')
 if header:header.parent.mkdir(parents=True,exist_ok=True);header.write_text('\n'.join(lines)+'\n')
 for name in ('LICENSE.txt','upstream.json'):
  shutil.copy2(ROOT/'experimental/framegen/vendor/fidelityfx-1.1.3'/name,output/name)
 (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 if proof:shutil.copy2(source/'unused-binding-proof.json',output/'unused-binding-proof.json')
 print(f'PASS: 18 native shaders verified/staged; binding metadata matches {report["profile"]}')
 return report
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--shaders',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);ap.add_argument('--header',type=Path);a=ap.parse_args();stage(a.shaders,a.output,a.header)
if __name__=='__main__':main()
