"""Check the native boundary constants against saved Unity physics queries."""
from pathlib import Path
import argparse,hashlib,json,math
def audit(a):
 raw=a.result.read_bytes();r=json.loads(raw)
 if r['unityVersion']!='2022.3.29f1' or len(r['shapes'])!=5:raise ValueError('unexpected physics reference')
 epsilon=2**-23;checked=0
 for sample in r['shapes']:
  if sample['messages'] or not all(sample[k] for k in ('rayX','rayY','rayZ')) or sample['overlaps']!=1:raise ValueError('reference shape/query did not initialize cleanly')
  for k,size,center in zip('xyz',(2,1.5,1),(-.62,0,-.12)):
   scale=sample['scale'][k];half=max(epsilon,abs(size*scale)*.5);expected=center*scale+half
   if abs(sample['closestPoint'][k]-expected)>max(1e-12,abs(expected)*2e-6):raise ValueError('box clamp or scaled-center mismatch')
   if abs(sample['boundsSize'][k]-half*2)>max(1e-12,half*2e-6):raise ValueError('box bounds mismatch')
   checked+=1
 frozen=r['frozen']
 for key in ('beforeVelocity','beforeAngularVelocity','afterVelocity','afterAngularVelocity'):
  if any(v!=0 for v in frozen[key].values()):raise ValueError('unexpected fully frozen velocity semantics')
 if frozen['beforePosition']!=frozen['afterPosition'] or frozen['beforeRotation']!=frozen['afterRotation']:raise ValueError('fully frozen reference moved')
 report={'engine':r['unityVersion'],'box_components_checked':checked,'box_minimum_half_extent':epsilon,'fully_frozen_velocity_rejected_immediately':True,'result_sha256':hashlib.sha256(raw).hexdigest(),'oracle_source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'scope':'box extent clamp/query boundaries and fully frozen velocity/pose; not full solver or gameplay parity','console_verified':False};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--result',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);audit(p.parse_args())
