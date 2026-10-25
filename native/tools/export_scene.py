"""Offline native scene conversion. Game bundles stay outside the source tree.

V1 coverage: active opaque MeshRenderers in the chosen scene, world-space
geometry, spatial batches and default camera. It deliberately does not claim
skinning, original shaders/textures, animation, scripts or enemy behavior.
"""
from pathlib import Path
import argparse,collections,hashlib,json,math,struct,sys,zlib

def identity():return [[1 if r==c else 0 for c in range(4)] for r in range(4)]
def mul(a,b):return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def trs(t):
 p=t['m_LocalPosition'];q=t['m_LocalRotation'];s=t['m_LocalScale'];x,y,z,w=(q[k] for k in ('x','y','z','w'))
 m=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),p['x']], [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),p['y']], [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),p['z']],[0,0,0,1]]
 for r in range(3):
  for c,k in enumerate(('x','y','z')):m[r][c]*=s[k]
 return m
def point(m,p):return tuple(sum(m[r][c]*p[c] for c in range(3))+m[r][3] for r in range(3))
def ref(p):
 if p.get('m_FileID',0)!=0:raise ValueError('external object reference')
 return p['m_PathID']
def normalize(p):
 n=math.sqrt(sum(v*v for v in p));return tuple(v/n for v in p) if n>1e-10 else (0,1,0)
def normal_matrix(m):
 a,b,c=m[0][:3];d,e,f=m[1][:3];g,h,i=m[2][:3]
 determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
 if abs(determinant)<1e-12:raise ValueError('singular transform')
 return [[(e*i-f*h)/determinant,(f*g-d*i)/determinant,(d*h-e*g)/determinant],[(c*h-b*i)/determinant,(a*i-c*g)/determinant,(b*g-a*h)/determinant],[(b*f-c*e)/determinant,(c*d-a*f)/determinant,(a*e-b*d)/determinant]]

def batch_matrix(submesh_count,root,owner,world):
 # Combined vertices are expressed relative to the batch root. A null root
 # therefore means world space, not the renderer GameObject's local space.
 return world(root if submesh_count else owner)

def inverse_affine(m):
 normal=normal_matrix(m);inverse=identity()
 for r in range(3):
  for c in range(3):inverse[r][c]=normal[c][r]
  inverse[r][3]=-sum(inverse[r][c]*m[c][3] for c in range(3))
 return inverse

def export(args):
 if args.python_lib:sys.path.insert(0,str(args.python_lib))
 import UnityPy
 from UnityPy.helpers.MeshHelper import MeshHandler
 env=UnityPy.load(str(args.bundle));objects={o.path_id:o for o in env.objects};types=collections.Counter(o.type.name for o in objects.values())
 trees={};transforms={};go_transform={};renderers={};filters={};filter_sources={};gos={};dynamic=set();loaded_models=False
 def tree(i):
  if i not in trees:trees[i]=objects[i].read_typetree()
  return trees[i]
 for i,o in objects.items():
  if o.type.name=='GameObject':gos[i]=tree(i)
  elif o.type.name in ('Transform','RectTransform'):
   t=tree(i);transforms[i]=t;go_transform[ref(t['m_GameObject'])]=i
  elif o.type.name=='MeshRenderer':renderers[ref(tree(i)['m_GameObject'])]=tree(i)
  elif o.type.name=='MeshFilter':
   go=ref(tree(i)['m_GameObject']);filters[go]=tree(i);filter_sources[go]=i
  elif o.type.name in ('Rigidbody','Animator'):dynamic.add(ref(tree(i)['m_GameObject']))
 worlds={};active_cache={};dynamic_cache={};visiting=set()
 def world(i):
  if not i:return identity()
  if i not in worlds:
   if i in visiting:raise ValueError('cyclic transform graph')
   visiting.add(i);t=transforms[i];worlds[i]=mul(world(ref(t['m_Father'])),trs(t));visiting.remove(i)
  return worlds[i]
 def active(i):
  if not i:return True
  if i not in active_cache:
   t=transforms[i];active_cache[i]=bool(gos[ref(t['m_GameObject'])]['m_IsActive']) and active(ref(t['m_Father']))
  return active_cache[i]
 def moving(i):
  if not i:return False
  if i not in dynamic_cache:
   t=transforms[i];dynamic_cache[i]=ref(t['m_GameObject']) in dynamic or moving(ref(t['m_Father']))
  return dynamic_cache[i]
 camera=None;camera_candidates=[]
 for i,o in objects.items():
  if o.type.name!='Camera':continue
  t=tree(i);go=ref(t['m_GameObject']);name=gos[go]['m_Name'];tag=gos[go].get('m_Tag',gos[go].get('m_TagString',''))
  if t.get('m_Enabled') and active(go_transform[go]) and not t.get('orthographic'):
   w=world(go_transform[go]);f=normalize(tuple(w[r][2] for r in range(3)))
   entry={'name':name,'position':[w[r][3] for r in range(3)],'yaw':math.atan2(f[0],f[2]),'pitch':math.asin(max(-1,min(1,f[1]))),'fov':t['field of view'],'mask':t['m_CullingMask']['m_Bits']}
   camera_candidates.append((0 if tag=='MainCamera' else 1 if 'main' in name.lower() else 2,entry))
 if not camera_candidates:raise ValueError('no active perspective camera; explicit camera selection required')
 camera=sorted(camera_candidates,key=lambda t:t[0])[0][1]
 cells={};mesh_cache={};counts=collections.Counter();errors=[]
 for go,mf in filters.items():
  renderer=renderers.get(go);tid=go_transform[go]
  if not renderer or not renderer.get('m_Enabled') or not active(tid):counts['inactive_or_disabled']+=1;continue
  if moving(tid):counts['dynamic_or_animated']+=1;continue
  if gos[go].get('m_Layer')==5 or not (camera['mask']&(1<<gos[go]['m_Layer'])):counts['camera_excluded']+=1;continue
  if renderer.get('m_CastShadows')==3:counts['shadows_only']+=1;continue
  try:
   mid=(mf['m_Mesh'].get('m_FileID',0),mf['m_Mesh']['m_PathID'])
   if mid not in mesh_cache:
    if mid[0]:
     if not loaded_models:
      models=args.models_bundle or args.bundle.parent/'assets_assets_assets/models.bundle'
      if not models.is_file():raise ValueError('external mesh requires models.bundle')
      env.load_file(str(models));loaded_models=True
     mesh=objects[filter_sources[go]].read().m_Mesh.read()
    else:mesh=objects[mid[1]].read()
    handler=MeshHandler(mesh);handler.process()
    mesh_cache[mid]=(handler,handler.get_triangles(),mesh)
   handler,submeshes,mesh=mesh_cache[mid]
   batch=renderer['m_StaticBatchInfo'];count=batch['subMeshCount'];start=batch['firstSubMesh'] if count else 0;end=start+count if count else len(submeshes)
   root=ref(renderer.get('m_StaticBatchRoot',{'m_PathID':0}));matrix=batch_matrix(count,root,tid,world);norm=normal_matrix(matrix)
   material=renderer.get('m_Materials',[]);vertices=handler.m_Vertices;normals=handler.m_Normals;uvs=handler.m_UV0
   for sub in range(start,end):
    # The first geometry backend uses explicit diagnostic material colours.
    # Original texture/shader bindings remain data in the migration manifest.
    mat=material[min(sub-start,len(material)-1)] if material else {'m_FileID':0,'m_PathID':0}
    digest=hashlib.sha256(json.dumps(mat,sort_keys=True).encode()).digest();rgb=tuple(70+v//2 for v in digest[:3]);color=rgb[0]|rgb[1]<<8|rgb[2]<<16|255<<24
    for triangle in submeshes[sub]:
     if len(triangle)!=3:raise ValueError('non-triangle topology')
     source=[int(v)+int(mesh.m_SubMeshes[sub].baseVertex or 0) for v in triangle]
     transformed=[point(matrix,vertices[v]) for v in source]
     key=tuple(math.floor(sum(p[c] for p in transformed)/3/args.cell_size) for c in range(3))
     cell=cells.setdefault(key,{'vertices':[],'indices':[],'lookup':{}})
     # Retain source winding; backface culling is disabled until visual parity.
     for v,p in zip(source,transformed):
      if not all(math.isfinite(x) and abs(x)<100000 for x in p):raise ValueError('invalid world vertex')
      n=normals[v][:3] if normals else (0,1,0)
      if not all(math.isfinite(x) for x in n):raise ValueError('invalid source normal')
      n=normalize(tuple(sum(norm[r][c]*n[c] for c in range(3)) for r in range(3)));uv=uvs[v][:2] if uvs else (0,0)
      if not all(math.isfinite(x) for x in uv):uv=(0,0);counts['undefined_uv_replaced']+=1
      packed=struct.pack('<8fI',*p,*n,*uv,color)
      index=cell['lookup'].get(packed)
      if index is None:index=len(cell['vertices']);cell['lookup'][packed]=index;cell['vertices'].append(packed)
      cell['indices'].append(index)
   counts['exported_renderers']+=1
  except (ValueError,KeyError,IndexError,TypeError,AttributeError) as error:
   errors.append({'object':go,'name':gos[go]['m_Name'],'reason':str(error)})
 if errors:raise RuntimeError('Scene geometry conversion failed: '+json.dumps(errors[:5]))
 vertices=bytearray();indices=bytearray();chunks=bytearray();vertex_count=index_count=0
 for key,cell in sorted(cells.items()):
  if not cell['indices']:continue
  points=[struct.unpack_from('<3f',v) for v in cell['vertices']];minimum=[min(p[c] for p in points) for c in range(3)];maximum=[max(p[c] for p in points) for c in range(3)]
  chunks+=struct.pack('<2I6f',index_count,len(cell['indices']),*minimum,*maximum)
  for v in cell['vertices']:vertices+=v
  for i in cell['indices']:indices+=struct.pack('<I',vertex_count+i)
  vertex_count+=len(cell['vertices']);index_count+=len(cell['indices'])
 if not vertex_count or not index_count:raise ValueError('no geometry converted')
 crc=zlib.crc32(vertices);crc=zlib.crc32(indices,crc);crc=zlib.crc32(chunks,crc)
 header=struct.pack('<8s4I6f2I',b'NXSCENE\0',1,vertex_count,index_count,len(chunks)//32,*camera['position'],camera['yaw'],camera['pitch'],camera['fov'],crc,0)
 args.output.parent.mkdir(parents=True,exist_ok=True)
 temporary=args.output.with_suffix('.tmp');temporary.write_bytes(header+vertices+indices+chunks);temporary.replace(args.output)
 report={'format':'NXSCENE1','source_bundle':str(args.bundle),'source_sha256':hashlib.sha256(args.bundle.read_bytes()).hexdigest(),'unitypy':UnityPy.__version__,'camera':camera,'source_objects':dict(types),'coverage':dict(counts),'conversion_errors':errors,'vertices':vertex_count,'triangles':index_count//3,'chunks':len(chunks)//32,'bytes':args.output.stat().st_size,'output_sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),'visual_parity':False,'gameplay_ported':False}
 if loaded_models:
  with models.open('rb') as stream:report['models_sha256']=hashlib.file_digest(stream,'sha256').hexdigest()
 args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('vertices','triangles','chunks','coverage','bytes')}));print('conversion errors',len(errors))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--models-bundle',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-lib',type=Path);p.add_argument('--cell-size',type=float,default=32);a=p.parse_args()
 if not math.isfinite(a.cell_size) or a.cell_size<=0:p.error('cell size must be positive')
 export(a)
