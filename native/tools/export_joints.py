"""Convert authored joint data, retaining all source fields in a sidecar.

Native anchors are in each rigid body's unscaled rotational frame. The
GameObject's full authored scale/shear is applied to anchor points once;
constraint axes are directions, not scaled mesh vectors. Auto-anchor matching
is performed when a constraint attaches, using the current native body poses.
"""
from pathlib import Path
import argparse,collections,gzip,hashlib,json,math,struct,zlib
from export_scene import identity,mul,trs,point
from export_physics import ref,quaternion_mul,rotate,vector

KINDS={'CharacterJoint':1,'FixedJoint':2,'ConfigurableJoint':3}
def number(v):
 if isinstance(v,dict) and v.get('$float')=='inf':return math.inf
 if not isinstance(v,(int,float)) or not math.isfinite(v):raise ValueError('unsupported joint numeric value')
 return float(v)
def unit(v):
 length=math.sqrt(sum(x*x for x in v))
 if not math.isfinite(length) or length<1e-6:raise ValueError('degenerate joint axis')
 return tuple(x/length for x in v)

def export(a):
 wb=a.world.read_bytes();source=json.loads(gzip.decompress(wb));records={o['id']:o for o in source['objects']};actors=a.actors.read_bytes();ah=struct.unpack_from('<8s8I',actors);graph=a.graph.read_bytes();gh=struct.unpack_from('<8s6I',graph)
 if ah[0]!=b'NXACTOR\0' or ah[1]!=1 or zlib.crc32(actors[40:])!=ah[6] or gh[0]!=b'NXGRAPH\0' or gh[1]!=1 or zlib.crc32(graph[32:])!=gh[5]:raise ValueError('invalid actor/hierarchy source pack')
 ar=json.loads(a.actors.with_suffix('.actors.json').read_text());gr=json.loads(a.graph.with_suffix('.graph.json').read_text());ws=hashlib.sha256(wb).hexdigest()
 if ar['world_sha256']!=ws or gr['world_sha256']!=ws or ar['pack_sha256']!=hashlib.sha256(actors).hexdigest() or gr['pack_sha256']!=hashlib.sha256(graph).hexdigest():raise ValueError('joint source identity mismatch')
 transforms={o['id']:o['data'] for o in records.values() if o['type'] in ('Transform','RectTransform')};by_go={ref(t['m_GameObject']):i for i,t in transforms.items()};worlds={};rotations={};visiting=set()
 def world(t):
  if t=='0':return identity(),(0,0,0,1)
  if t not in worlds:
   if t in visiting:raise ValueError('joint hierarchy cycle')
   visiting.add(t);p,q=world(ref(transforms[t]['m_Father']));worlds[t]=mul(p,trs(transforms[t]));rotations[t]=quaternion_mul(q,tuple(transforms[t]['m_LocalRotation'][k] for k in 'xyzw'));visiting.remove(t)
  return worlds[t],rotations[t]
 bodies={};owner={}
 for i in range(ah[2]):
  values=struct.unpack_from('<qq8I20f',actors,40+i*128);bodies[str(values[0])]=str(values[1]);owner[str(values[1])]=values[0]
 def anchor(go,raw):
  matrix,q=world(by_go[go]);p=point(matrix,raw);origin=point(matrix,(0,0,0));inverse=(-q[0],-q[1],-q[2],q[3]);return rotate(inverse,tuple(p[i]-origin[i] for i in range(3)))
 payload=bytearray();details=[];counts=collections.Counter()
 for o in sorted((o for o in records.values() if o['type'] in KINDS),key=lambda o:int(o['id'])):
  t=o['data'];go=ref(t['m_GameObject']);kind=KINDS[o['type']];body=owner.get(go);connected=ref(t['m_ConnectedBody'])
  if body is None or (connected!='0' and connected not in bodies) or connected==str(body):raise ValueError('invalid joint body ownership')
  if ref(t['m_ConnectedArticulationBody'])!='0':raise ValueError('articulation joint adapter required')
  auto=bool(t.get('m_AutoConfigureConnectedAnchor',True));projection=t.get('m_ProjectionMode',int(t.get('m_EnableProjection',False)))
  flags=(1 if t.get('m_Enabled',True) else 0)|(2 if auto else 0)|(4 if t['m_EnableCollision'] else 0)|(8 if t['m_EnablePreprocessing'] else 0)|(16 if t.get('m_ConfiguredInWorldSpace') else 0)|(32 if t.get('m_SwapBodies') else 0)
  raw_anchor=vector(t.get('m_Anchor',dict(x=0,y=0,z=0)));raw_connected=vector(t.get('m_ConnectedAnchor',dict(x=0,y=0,z=0)));aa=anchor(go,raw_anchor);ca=anchor(bodies[connected],raw_connected) if connected!='0' else raw_connected
  axis=vector(t.get('m_Axis',dict(x=1,y=0,z=0)));secondary=vector(t.get('m_SwingAxis',t.get('m_SecondaryAxis',dict(x=0,y=1,z=0))));ax=unit(axis)
  if sum((secondary[i]-sum(ax[c]*secondary[c] for c in range(3))*ax[i])**2 for i in range(3))<1e-12:counts['degenerate_secondary_retained']+=1
  empty_limit=dict(limit=0,bounciness=0,contactDistance=0)
  limits=[t.get('m_LinearLimit',empty_limit),t.get('m_LowTwistLimit',t.get('m_LowAngularXLimit',empty_limit)),t.get('m_HighTwistLimit',t.get('m_HighAngularXLimit',empty_limit)),t.get('m_Swing1Limit',t.get('m_AngularYLimit',empty_limit)),t.get('m_Swing2Limit',t.get('m_AngularZLimit',empty_limit))]
  springs=[]
  for k in [('m_TwistLimitSpring','m_AngularXLimitSpring'),('m_SwingLimitSpring',),('m_LinearLimitSpring',),('m_AngularYZLimitSpring',)]:
   v=next((t[key] for key in k if key in t),dict(spring=0,damper=0));springs.extend([number(v['spring']),number(v['damper'])])
  motion=[t.get('m_'+key+'Motion',0 if kind==2 or i<3 else 1) for i,key in enumerate(['X','Y','Z','AngularX','AngularY','AngularZ'])]
  target=[]
  for key in ['m_TargetPosition','m_TargetVelocity','m_TargetAngularVelocity']:target.extend(vector(t.get(key,dict(x=0,y=0,z=0))))
  rotation=t.get('m_TargetRotation',dict(x=0,y=0,z=0,w=1));target.extend(rotation[k] for k in 'xyzw')
  drives=[]
  for key in ['m_XDrive','m_YDrive','m_ZDrive','m_AngularXDrive','m_AngularYZDrive','m_SlerpDrive']:
   d=t.get(key,dict(positionSpring=0,positionDamper=0,maximumForce=0,useAcceleration=0));drives.extend(number(d[k]) for k in ['positionSpring','positionDamper','maximumForce','useAcceleration'])
  common=[number(t.get('m_ProjectionDistance',.1)),number(t.get('m_ProjectionAngle',180)),number(t['m_BreakForce']),number(t['m_BreakTorque']),number(t['m_MassScale']),number(t['m_ConnectedMassScale'])]
  all_floats=list(aa)+list(ca)+list(axis)+list(secondary)+[number(v['limit']) for v in limits]+common+springs+target+drives+[number(v[k]) for v in limits for k in ['bounciness','contactDistance']]
  payload.extend(struct.pack('<4q4I31f6I47f32I',int(o['id']),body,int(connected),int(go),kind,flags,projection,t.get('m_RotationDriveMode',0),*all_floats[:31],*motion,*all_floats[31:],*([0]*32)))
  details.append({'id':o['id'],'type':o['type'],'body':str(body),'connected':connected,'anchor_frame':'rigid local, authored GameObject scale/shear applied once','source':t});counts[o['type']]+=1;counts['auto_anchor']+=auto;counts['projection_requested']+=bool(projection)
 if len(payload)!=512*len(details):raise ValueError('joint ABI mismatch')
 header=struct.pack('<8s8I',b'NXJOINT\0',1,len(details),ah[6],gh[5],zlib.crc32(payload),0,0,0);binary=header+payload;a.output.parent.mkdir(parents=True,exist_ok=True);temp=a.output.with_name(a.output.name+'.tmp');temp.write_bytes(binary);temp.replace(a.output)
 report={'format':'NXJOINT1','bytes':len(binary),'joints':len(details),'counts':dict(counts),'pack_sha256':hashlib.sha256(binary).hexdigest(),'world_sha256':ws,'actor_sha256':hashlib.sha256(actors).hexdigest(),'graph_sha256':hashlib.sha256(graph).hexdigest(),'records':details,'solver_parity_verified':False,'runtime_connected':False};a.output.with_suffix('.joints.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--world',type=Path,required=True);p.add_argument('--actors',type=Path,required=True);p.add_argument('--graph',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
