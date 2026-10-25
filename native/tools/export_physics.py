"""Export authored colliders, hierarchy, player configuration and layer masks.

Unlike render geometry, this retains inactive objects, triggers and dynamic
body ownership. The runtime must report which records it has instantiated.
"""
from pathlib import Path
import argparse,collections,gzip,hashlib,json,math,re,struct,sys,zlib
from export_scene import identity,mul,trs,point,normalize

KINDS={'BoxCollider':1,'MeshCollider':2,'SphereCollider':3,'CapsuleCollider':4}
SURFACE_FLAGS={'push':2,'canJump':4,'silentJumpFail':8,'canWallJump':16,'canSlide':32,'silentSlideFail':64,'canDash':128,'silentDashFail':256,'canClimbStep':512,'launchable':1024,'forceCrouch':2048,'overrideFootsteps':4096,'overrideSurfaceType':8192,'dontRotateCamera':16384}
def quaternion_mul(a,b):
 ax,ay,az,aw=a;bx,by,bz,bw=b
 return (aw*bx+ax*bw+ay*bz-az*by,aw*by-ax*bz+ay*bw+az*bx,aw*bz+ax*by-ay*bx+az*bw,aw*bw-ax*bx-ay*by-az*bz)
def rotate(q,v):
 def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
 t=cross(q[:3],v);u=cross(q[:3],t)
 return tuple(v[i]+2*q[3]*t[i]+2*u[i] for i in range(3))
def ref(pointer):
 value=pointer['$ref']
 if value['file']:raise ValueError('unexpected external scene object reference')
 return value['path']
def vector(value):return tuple(float(value[k]) for k in 'xyz')

def export(args):
 if args.python_lib:sys.path.insert(0,str(args.python_lib))
 import UnityPy
 from UnityPy.helpers.MeshHelper import MeshHandler
 authored=json.load(gzip.open(args.world,'rt',encoding='utf-8'));records={o['id']:o for o in authored['objects']}
 if hashlib.sha256(args.bundle.read_bytes()).hexdigest()!=authored['bundle_sha256']:raise ValueError('world and mesh source bundles differ')
 env=UnityPy.load(str(args.bundle));source={o.path_id:o for o in env.objects}
 default_resources=args.bundle.parents[2]/'bin/Data/unity default resources'
 if default_resources.is_file():env.load_file(str(default_resources))
 material_bundle=args.bundle.parent/'assets_assets_assets/physicsmaterials.bundle'
 env.load_file(str(material_bundle))
 globals_path=args.bundle.parents[2]/'bin/Data/globalgamemanagers'
 global_env=UnityPy.load(str(globals_path));tag_manager=next(o.read_typetree() for o in global_env.objects if o.type.name=='TagManager')
 def ground_tags(go):
  tag=records[go]['data']['m_Tag']
  if not isinstance(tag,int):raise ValueError('unexpected serialized tag type')
  if tag<20000:return 0
  index=tag-20000
  if index>=len(tag_manager['tags']):raise ValueError('tag outside source TagManager')
  return {'Floor':1,'Slippery':2,'Moving':4}.get(tag_manager['tags'][index],0)
 materials={}
 def material(record):
  pointer=record['data']['m_Material']['$ref'];key=(pointer['file'],pointer['path'])
  if key not in materials:
   if pointer['path']=='0':materials[key]=(.6,.6,0.,0,0)
   else:
    tree=source[int(record['id'])].read().m_Material.read().object_reader.read_typetree()
    materials[key]=(float(tree['dynamicFriction']),float(tree['staticFriction']),float(tree['bounciness']),int(tree['frictionCombine']),int(tree['bounceCombine']))
  return materials[key]
 transforms={o['id']:o['data'] for o in records.values() if o['type'] in ('Transform','RectTransform')}
 by_go=collections.defaultdict(list);go_transform={}
 for o in records.values():
  if 'm_GameObject' in o['data']:by_go[ref(o['data']['m_GameObject'])].append(o)
  if o['id'] in transforms:go_transform[ref(o['data']['m_GameObject'])]=o['id']
 cache={};active_cache={};body_cache={};rotation_cache={};visiting=set()
 def rotation(t):
  if t=='0':return (0,0,0,1)
  if t not in rotation_cache:rotation_cache[t]=quaternion_mul(rotation(ref(transforms[t]['m_Father'])),tuple(transforms[t]['m_LocalRotation'][k] for k in 'xyzw'))
  return rotation_cache[t]
 def world(t):
  if t=='0':return identity()
  if t not in cache:
   if t in visiting:raise ValueError('transform cycle')
   visiting.add(t);cache[t]=mul(world(ref(transforms[t]['m_Father'])),trs(transforms[t]));visiting.remove(t)
  return cache[t]
 def active(t):
  if t=='0':return True
  if t not in active_cache:active_cache[t]=bool(records[ref(transforms[t]['m_GameObject'])]['data']['m_IsActive']) and active(ref(transforms[t]['m_Father']))
  return active_cache[t]
 def body(t):
  if t=='0':return 0
  if t not in body_cache:
   go=ref(transforms[t]['m_GameObject']);owned=[o for o in by_go[go] if o['type']=='Rigidbody']
   body_cache[t]=int(owned[0]['id']) if owned else body(ref(transforms[t]['m_Father']))
  return body_cache[t]
 def surface(go,owner):
  owner_go=ref(records[str(owner)]['data']['m_GameObject']) if owner else go
  candidates=[o for o in by_go[owner_go] if (o.get('script') or {}).get('class')=='CustomGroundProperties']
  if not candidates:return (0,1.,1.,0.,0.,0.,1.,0,0)
  record=candidates[0];data=record['data'];push=vector(data['pushForce'])
  if data['pushDirectionRelative']:push=rotate(rotation(go_transform[owner_go]),push)
  flags=1|sum(flag for key,flag in SURFACE_FLAGS.items() if data[key])
  return (int(record['id']),float(data['friction']),float(data['speedMultiplier']),*push,float(data['jumpForceMultiplier']),flags,0)
 physics_text=args.settings.read_text();gravity_match=re.search(r'm_Gravity: \{x: ([^,]+), y: ([^,]+), z: ([^}]+)\}',physics_text)
 if not re.search(r'm_DefaultMaterial: \{fileID: 0\}',physics_text):raise ValueError('non-default project physics material needs explicit resolution')
 gravity=tuple(map(float,gravity_match.groups()));mask_hex=re.search(r'm_LayerCollisionMatrix: ([0-9a-fA-F]+)',physics_text)[1]
 layer_bytes=bytes.fromhex(mask_hex)
 if len(layer_bytes)!=128:raise ValueError('invalid layer collision matrix')
 players=[o for o in records.values() if (o.get('script') or {}).get('class')=='NewMovement']
 if len(players)!=1:raise ValueError('explicit player selection required')
 player=players[0]['data'];player_go=ref(player['m_GameObject']);components=by_go[player_go]
 rb=next(o['data'] for o in components if o['type']=='Rigidbody');capsule_record=next(o for o in components if o['type']=='CapsuleCollider');capsule=capsule_record['data']
 if capsule['m_Direction']!=1:raise ValueError('player capsule must be vertical')
 pw=world(go_transform[player_go]);position=point(pw,(0,0,0));up=normalize(tuple(pw[r][1] for r in range(3)));forward=normalize(tuple(pw[r][2] for r in range(3)))
 if any(abs(sum(pw[r][c]**2 for r in range(3))-1)>1e-4 for c in range(3)):raise ValueError('scaled player requires an explicit physics adapter')
 scene_meta=json.loads(args.scene_meta.read_text())
 if scene_meta['source_sha256']!=authored['bundle_sha256']:raise ValueError('camera and physics scene sources differ')
 camera=scene_meta['camera'];offset=tuple(camera['position'][i]-position[i] for i in range(3))
 profile_values=(*position,*up,*forward,*offset,player['walkSpeed'],player['airAcceleration'],player['jumpPower'],player['wallJumpPower'],rb['m_Mass'],capsule['m_Radius'],capsule['m_Height'],*vector(capsule['m_Center']),*gravity)
 assert len(profile_values)==25
 sensor_ids=[]
 for field in ('gc','slopeCheck'):
  group=records[ref(player[field])]['data'];instances=group['instances']
  if len(instances)!=1:raise ValueError('initial multi-instance ground group requires explicit native bindings')
  ground=records[ref(instances[0])]['data'];sensor_ids.append(int(ref(ground['capsule'])))
 profile=struct.pack('<q28f2I2q',int(player_go),*profile_values,*material(capsule_record),*sensor_ids)
 loaded_models=False;meshes={};vertices=bytearray();indices=bytearray();colliders=bytearray();counts=collections.Counter()
 def mesh_geometry(record):
  nonlocal loaded_models
  pointer=record['data']['m_Mesh']['$ref'];key=(pointer['file'],pointer['path'])
  if key[1]=='0':return (0,0,0,0)
  if key not in meshes:
   if pointer['file'] and not loaded_models:env.load_file(str(args.models_bundle or args.bundle.parent/'assets_assets_assets/models.bundle'));loaded_models=True
   mesh=source[int(record['id'])].read().m_Mesh.read();handler=MeshHandler(mesh);handler.process()
   first_vertex=len(vertices)//12;first_index=len(indices)//4
   for v in handler.m_Vertices:
    if not all(math.isfinite(x) for x in v[:3]):raise ValueError('nonfinite collision mesh vertex')
    vertices.extend(struct.pack('<3f',*v[:3]))
   for sub,triangles in enumerate(handler.get_triangles()):
    for triangle in triangles:
     if len(triangle)!=3:raise ValueError('collision topology is not triangles')
     for v in triangle:
      local=int(v)+int(mesh.m_SubMeshes[sub].baseVertex or 0)
      if not 0<=local<len(handler.m_Vertices):raise ValueError('collision index outside mesh')
      indices.extend(struct.pack('<I',first_vertex+local))
   meshes[key]=(first_vertex,len(handler.m_Vertices),first_index,len(indices)//4-first_index)
  return meshes[key]
 for record in records.values():
  if record['type'] not in KINDS:continue
  data=record['data'];go=ref(data['m_GameObject']);t=go_transform[go];matrix=world(t);owner=body(t)
  flags=(1 if active(t) else 0)|(2 if data.get('m_Enabled',True) else 0)|(4 if data.get('m_IsTrigger') else 0)|(8 if owner else 0)|(16 if data.get('m_Convex') else 0)
  shape=mesh_geometry(record) if record['type']=='MeshCollider' else (0,0,0,0)
  center=vector(data.get('m_Center',dict(x=0,y=0,z=0)));size=vector(data.get('m_Size',dict(x=0,y=0,z=0)))
  # Capsule direction is carried in size[0]; the other components are reserved.
  if record['type']=='CapsuleCollider':size=(float(data['m_Direction']),0,0)
  values=tuple(v for row in matrix[:3] for v in row)+center+size+(float(data.get('m_Radius',0)),float(data.get('m_Height',0)))
  if not all(math.isfinite(v) for v in values):raise ValueError('nonfinite collider transform/shape '+record['id'])
  colliders.extend(struct.pack('<3q8I23f3Iq6f2I',int(record['id']),int(go),owner,KINDS[record['type']],records[go]['data']['m_Layer'],flags,*shape,ground_tags(go),*values,*material(record),0,*surface(go,owner)))
  counts[record['type']]+=1
  if flags&3==3:
   counts['active_enabled']+=1
   counts['active_dynamic' if owner else 'active_static']+=1
   if flags&4:counts['active_trigger']+=1
 payload=profile+layer_bytes+colliders+vertices+indices
 header=struct.pack('<8s6I',b'NXPHYS\0\0',3,len(colliders)//200,len(vertices)//12,len(indices)//4,zlib.crc32(payload),0)
 args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(header+payload)
 report={'format':'NXPHYS1','source_sha256':authored['bundle_sha256'],'world_sha256':hashlib.sha256(args.world.read_bytes()).hexdigest(),'physics_settings_sha256':hashlib.sha256(args.settings.read_bytes()).hexdigest(),'pack_sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),'counts':dict(counts),'unique_collision_meshes':len(meshes),'vertices':len(vertices)//12,'triangles':len(indices)//12,'player':{'object':player_go,'position':position,'walk_speed':player['walkSpeed'],'air_acceleration':player['airAcceleration'],'mass':rb['m_Mass'],'capsule_radius':capsule['m_Radius'],'capsule_height':capsule['m_Height'],'gravity':gravity},'solver_parity_verified':False,'gameplay_complete':False}
 report['format']='NXPHYS3';report['player']['ground_sensor_colliders']=sensor_ids
 report['dependencies']={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in (args.scene_meta,material_bundle,default_resources,globals_path) if path.is_file()}
 if loaded_models:
  models=args.models_bundle or args.bundle.parent/'assets_assets_assets/models.bundle';report['dependencies'][str(models)]=hashlib.sha256(models.read_bytes()).hexdigest()
 args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--world',type=Path,required=True);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--scene-meta',type=Path,required=True);p.add_argument('--settings',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--models-bundle',type=Path);p.add_argument('--python-lib',type=Path);export(p.parse_args())
