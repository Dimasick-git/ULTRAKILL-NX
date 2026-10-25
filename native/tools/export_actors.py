"""Retain authored rigid bodies and exact compound-collider ownership.

Joint source fields remain in the full world snapshot. This pack identifies
joint-dependent bodies explicitly; it does not approximate ragdolls as loose
independent bodies. Collision geometry is referenced from the matching pack.
"""
from pathlib import Path
import argparse,collections,gzip,hashlib,json,math,struct,zlib
from export_scene import identity,mul,trs
from export_physics import quaternion_mul,ref,vector

def export(a):
 world_bytes=a.world.read_bytes();world=json.loads(gzip.decompress(world_bytes));records={o['id']:o for o in world['objects']}
 graph=a.graph.read_bytes();gh=struct.unpack_from('<8s6I',graph)
 physics=a.physics.read_bytes();ph=struct.unpack_from('<8s6I',physics)
 if gh[0]!=b'NXGRAPH\0' or gh[1]!=1 or zlib.crc32(graph[32:])!=gh[5]:raise ValueError('invalid graph source')
 if ph[0]!=b'NXPHYS\0\0' or ph[1]!=3 or zlib.crc32(physics[32:])!=ph[5]:raise ValueError('invalid physics source')
 gr=json.loads(a.graph.with_suffix('.graph.json').read_text());pr=json.loads(a.physics.with_suffix('.json').read_text())
 world_sha=hashlib.sha256(world_bytes).hexdigest()
 if gr['world_sha256']!=world_sha or pr['world_sha256']!=world_sha or gr['pack_sha256']!=hashlib.sha256(graph).hexdigest() or pr['pack_sha256']!=hashlib.sha256(physics).hexdigest():raise ValueError('actor source identity mismatch')
 transforms={o['id']:o['data'] for o in records.values() if o['type'] in ('Transform','RectTransform')};go_transform={ref(t['m_GameObject']):i for i,t in transforms.items()};worlds={};rotations={};active_cache={};visiting=set()
 def pose(t):
  if t=='0':return identity(),(0,0,0,1),True
  if t not in worlds:
   if t in visiting:raise ValueError('actor transform cycle')
   visiting.add(t);p,q,active=pose(ref(transforms[t]['m_Father']));worlds[t]=mul(p,trs(transforms[t]));rotations[t]=quaternion_mul(q,tuple(transforms[t]['m_LocalRotation'][k] for k in 'xyzw'));active_cache[t]=active and bool(records[ref(transforms[t]['m_GameObject'])]['data']['m_IsActive']);visiting.remove(t)
  return worlds[t],rotations[t],active_cache[t]
 collider_refs=collections.defaultdict(list)
 for i in range(ph[2]):
  cid,go,body=struct.unpack_from('<qqq',physics,304+i*200)
  if body:collider_refs[body].append((i,str(go)))
 joints=collections.defaultdict(list)
 for o in records.values():
  if o['type'] in ('CharacterJoint','ConfigurableJoint','FixedJoint','HingeJoint','SpringJoint'):joints[ref(o['data']['m_GameObject'])].append(o['id'])
 player_id=struct.unpack_from('<q',physics,32)[0];body_bytes=bytearray();ref_bytes=bytearray();report=[];counts=collections.Counter()
 for o in sorted((o for o in records.values() if o['type']=='Rigidbody'),key=lambda x:int(x['id'])):
  t=o['data'];go=ref(t['m_GameObject']);matrix,q,active=pose(go_transform[go]);p=[matrix[i][3] for i in range(3)];body_id=int(o['id']);refs=collider_refs.pop(body_id,[])
  if t.get('m_IncludeLayers',{}).get('m_Bits',0) or t.get('m_ExcludeLayers',{}).get('m_Bits',0):raise ValueError('rigidbody layer overrides require native support')
  refs_with_state=[]
  for index,collider_go in refs:
   relative=True;cursor=go_transform[collider_go];root=go_transform[go]
   while cursor!=root:
    if cursor=='0':raise ValueError('collider body is not an ancestor')
    relative=relative and bool(records[ref(transforms[cursor]['m_GameObject'])]['data']['m_IsActive']);cursor=ref(transforms[cursor]['m_Father'])
   refs_with_state.append((index,int(relative)))
  flags=int(active)|(2 if t['m_IsKinematic'] else 0)|(4 if t['m_UseGravity'] else 0)|(8 if t['m_ImplicitCom'] else 0)|(16 if t['m_ImplicitTensor'] else 0)|(32 if int(go)==player_id else 0)|(64 if go in joints else 0)
  values=p+list(q)+[t['m_Mass'],t['m_Drag'],t['m_AngularDrag']]+list(vector(t['m_CenterOfMass']))+list(vector(t['m_InertiaTensor']))+[t['m_InertiaRotation'][k] for k in 'xyzw']
  if not all(math.isfinite(v) for v in values) or t['m_Mass']<=0 or t['m_Drag']<0 or t['m_AngularDrag']<0:raise ValueError('invalid authored body numeric state')
  if t['m_Constraints']&~126 or t['m_Interpolate'] not in range(3) or t['m_CollisionDetection'] not in range(4):raise ValueError('unsupported source rigidbody flags')
  body_bytes.extend(struct.pack('<qq8I20f',body_id,int(go),flags,records[go]['data']['m_Layer'],t['m_Constraints'],t['m_CollisionDetection'],len(ref_bytes)//8,len(refs),t['m_Interpolate'],0,*values));ref_bytes.extend(b''.join(struct.pack('<II',*v) for v in refs_with_state))
  report.append({'id':o['id'],'object':go,'name':records[go]['data']['m_Name'],'flags':flags,'colliders':refs_with_state,'joint_ids':joints.get(go,[]),'active':active});counts['active' if active else 'inactive']+=1;counts['kinematic' if t['m_IsKinematic'] else 'dynamic']+=1;counts['joint_dependent']+=bool(go in joints);counts['without_colliders']+=not refs
 if collider_refs:raise ValueError('collider refers to a missing rigidbody')
 payload=body_bytes+ref_bytes;binary=struct.pack('<8s8I',b'NXACTOR\0',1,len(report),len(ref_bytes)//8,gh[5],ph[5],zlib.crc32(payload),0,0)+payload
 a.output.parent.mkdir(parents=True,exist_ok=True);temp=a.output.with_name(a.output.name+'.tmp');temp.write_bytes(binary);temp.replace(a.output)
 result={'format':'NXACTOR1','pack_sha256':hashlib.sha256(binary).hexdigest(),'world_sha256':world_sha,'graph_sha256':hashlib.sha256(graph).hexdigest(),'physics_sha256':hashlib.sha256(physics).hexdigest(),'bodies':len(report),'collider_refs':len(ref_bytes)//8,'counts':dict(counts),'records':report,'bytes':len(binary),'joint_solver_connected':False,'complete_gameplay':False}
 a.output.with_suffix('.actors.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='records'},indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--world',type=Path,required=True);p.add_argument('--graph',type=Path,required=True);p.add_argument('--physics',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
