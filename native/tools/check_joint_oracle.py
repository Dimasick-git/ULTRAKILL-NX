"""Audit the saved Unity 2022.3 joint reference, not full solver parity."""
from pathlib import Path
import argparse,hashlib,json,math

def audit(a):
 raw=a.result.read_bytes();r=json.loads(raw);samples=r['samples']
 if r['unityVersion']!='2022.3.29f1' or len(samples)!=204:raise ValueError('unexpected reference engine/case count')
 def values(v):return tuple(v[k] for k in 'xyz')
 def near(a,b,tolerance):return max(abs(x-y) for x,y in zip(a,b))<=tolerance
 def qnear(a,b):
  av=tuple(a[k] for k in 'xyzw');bv=tuple(b[k] for k in 'xyzw');return min(max(abs(x-y) for x,y in zip(av,bv)),max(abs(x+y) for x,y in zip(av,bv)))<=1e-5
 checked=0;max_position=0;angles=[]
 for s in samples:
  for key in ('position','angularVelocity','before','after','normalizedAxis','normalizedSwing'):
   if not all(math.isfinite(v) for v in s[key].values()):raise ValueError('nonfinite reference result')
  if s['messages']:raise ValueError('physics reference emitted warnings/errors: '+str(s['messages']))
  if any(values(s['requestedSwing'])):continue
  axis=values(s['requestedAxis']);length=math.sqrt(sum(x*x for x in axis));x,y,z=(v/length for v in axis)
  expected=(0,-z,y) if abs(z)>math.sqrt(.5) else (-y,x,0);length=math.sqrt(sum(v*v for v in expected));expected=tuple(v/length for v in expected)
  if not near(expected,values(s['normalizedSwing']),1e-6):raise ValueError('native orthogonal basis differs from reference API')
  candidates=[t for t in samples if t['requestedAxis']==s['requestedAxis'] and t['initialAngularVelocity']==s['initialAngularVelocity'] and t['requestedSwing']==s['normalizedSwing'] and ('-serialized-' in t['name'])==('-serialized-' in s['name'])]
  if not candidates:raise ValueError('missing explicit orthogonal-axis comparison')
  for t in candidates:
   error=math.sqrt(sum((s['position'][k]-t['position'][k])**2 for k in 'xyz'));max_position=max(max_position,error)
   if error>1e-4 or not qnear(s['after'],t['after']):raise ValueError('zero-secondary behavior differs from explicit orthogonal basis')
  checked+=1
  if axis==(0,1,0) and s['initialAngularVelocity']['x']==0 and s['initialAngularVelocity']['z']==0:
   angle=math.degrees(2*math.atan2(s['after']['y'],s['after']['w']));expected_angle=20 if s['initialAngularVelocity']['y']>0 else -70
   if abs(angle-expected_angle)>.001:raise ValueError('unexpected original twist limit sign')
   angles.append(angle)
 if checked!=48 or len(angles)!=4:raise ValueError('reference boundary coverage incomplete')
 report={'engine':r['unityVersion'],'cases':len(samples),'zero_secondary_comparisons':checked,'maximum_position_difference':max_position,'owner_twist_boundary_angles':angles,'result_sha256':hashlib.sha256(raw).hexdigest(),'oracle_source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'scope':'native OrthoNormalize basis and connected-relative-to-owner twist signs; not complete PhysX/Jolt solver or gameplay parity','console_verified':False}
 a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--result',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);audit(p.parse_args())
