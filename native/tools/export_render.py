"""Preserve MeshRenderer ownership and inactive geometry for native runtime.

Vertices are renderer-local, including inverse conversion of Unity static
batches. Materials remain exact external references; this is not yet a shader
or texture translator. MeshRenderer transforms follow the hierarchy, including
rigid bodies and animation rest poses. Skinned deformation needs its importer.
"""
from pathlib import Path
import argparse,collections,hashlib,json,math,struct,sys,zlib
from export_scene import identity,mul,trs,point,ref,normalize,normal_matrix,batch_matrix,inverse_affine

def vertex_color(mesh,handler,index):
 if not handler.m_Colors:return 0xffffffff
 rgba=handler.m_Colors[index];channels=mesh.m_VertexData.m_Channels
 compressed=getattr(getattr(mesh,'m_CompressedMesh',None),'m_Colors',None)
 packed_colors=compressed and compressed.m_NumItems
 # MeshHandler exposes normalized vertex channels as their raw integer dtype.
 # Decode by the serialized format, not by the observed brightness/range.
 fmt=channels[3].format if len(channels)>3 and channels[3].dimension else 0
 if not packed_colors and fmt==2:rgba=tuple(x/255 for x in rgba)
 elif not packed_colors and fmt not in (0,1):raise ValueError('unsupported vertex color format: '+str(fmt))
 if len(rgba)!=4 or not all(math.isfinite(x) and 0<=x<=1 for x in rgba):raise ValueError('unsupported source vertex color range: '+repr(rgba))
 return sum(round(channel*255)<<(8*i) for i,channel in enumerate(rgba))

def export(a):
 if a.python_lib:sys.path.insert(0,str(a.python_lib))
 import UnityPy
 from UnityPy.helpers.MeshHelper import MeshHandler
 graph=a.graph.read_bytes();gh=struct.unpack_from('<8s6I',graph)
 if gh[0]!=b'NXGRAPH\0' or gh[1]!=1 or zlib.crc32(graph[32:])!=gh[5]:raise ValueError('invalid source hierarchy pack')
 graph_report=json.loads(a.graph.with_suffix('.graph.json').read_text());source_sha=hashlib.sha256(a.bundle.read_bytes()).hexdigest()
 if graph_report['source_bundle_sha256']!=source_sha or graph_report['pack_sha256']!=hashlib.sha256(graph).hexdigest():raise ValueError('render/hierarchy source mismatch')
 strings=graph[32+gh[2]*88+gh[3]*32:];graph_renderers={}
 for i in range(gh[3]):
  cid,node,to,tl,so,sl,flags=struct.unpack_from('<q6I',graph,32+gh[2]*88+i*32)
  if strings[to:to+tl].decode()=='MeshRenderer':graph_renderers[cid]=node
 env=UnityPy.load(str(a.bundle));objects={o.path_id:o for o in env.objects};trees={};transforms={};go_transform={};gos={};filters={};renderers={};moving_gos=set()
 default_resources=a.bundle.parents[2]/'bin/Data/unity default resources'
 if default_resources.is_file():env.load_file(str(default_resources))
 def tree(i):
  if i not in trees:trees[i]=objects[i].read_typetree()
  return trees[i]
 for i,o in objects.items():
  if o.type.name=='GameObject':gos[i]=tree(i)
  elif o.type.name in ('Transform','RectTransform'):
   t=tree(i);transforms[i]=t;go_transform[ref(t['m_GameObject'])]=i
  elif o.type.name=='MeshFilter':filters[ref(tree(i)['m_GameObject'])]=i
  elif o.type.name=='MeshRenderer':renderers[ref(tree(i)['m_GameObject'])]=i
  elif o.type.name in ('Rigidbody','Animator'):moving_gos.add(ref(tree(i)['m_GameObject']))
 worlds={};active_cache={};moving_cache={}
 def world(i):
  if not i:return identity()
  if i not in worlds:worlds[i]=mul(world(ref(transforms[i]['m_Father'])),trs(transforms[i]))
  return worlds[i]
 def active(i):
  if not i:return True
  if i not in active_cache:active_cache[i]=bool(gos[ref(transforms[i]['m_GameObject'])]['m_IsActive']) and active(ref(transforms[i]['m_Father']))
  return active_cache[i]
 def moving(i):
  if not i:return False
  if i not in moving_cache:moving_cache[i]=ref(transforms[i]['m_GameObject']) in moving_gos or moving(ref(transforms[i]['m_Father']))
  return moving_cache[i]
 cameras=[]
 for i,o in objects.items():
  if o.type.name!='Camera':continue
  t=tree(i);go=ref(t['m_GameObject']);tid=go_transform[go]
  if not t.get('m_Enabled') or not active(tid) or t.get('orthographic'):continue
  matrix=world(tid);forward=normalize(tuple(matrix[r][2] for r in range(3)));name=gos[go]['m_Name'];tag=gos[go].get('m_Tag',gos[go].get('m_TagString',''))
  camera={'position':[matrix[r][3] for r in range(3)],'yaw':math.atan2(forward[0],forward[2]),'pitch':math.asin(max(-1,min(1,forward[1]))),'fov':t['field of view'],'mask':t['m_CullingMask']['m_Bits']}
  cameras.append((0 if tag=='MainCamera' else 1 if 'main' in name.lower() else 2,camera))
 if not cameras:raise ValueError('no authored perspective camera')
 camera=sorted(cameras,key=lambda v:v[0])[0][1];cache={};models_loaded=False;counts=collections.Counter();errors=[];owners=[];vertices=bytearray();indices=bytearray();chunks=bytearray();vertex_count=index_count=0
 for go,renderer_id in sorted(renderers.items()):
  if go not in filters:counts['no_mesh_filter']+=1;continue
  tid=go_transform[go]
  if moving(tid):counts['rigid_or_animated_rest_pose_retained']+=1
  renderer=tree(renderer_id);mf=tree(filters[go]);mid=(mf['m_Mesh'].get('m_FileID',0),mf['m_Mesh']['m_PathID'])
  if mid[1]==0:counts['no_mesh_assigned']+=1;continue
  try:
   if renderer_id not in graph_renderers:raise ValueError('renderer missing from hierarchy pack')
   if mid not in cache:
    if mid[0]:
     if not models_loaded:env.load_file(str(a.models_bundle));models_loaded=True
     source=objects[filters[go]].read().m_Mesh.read()
    else:source=objects[mid[1]].read()
    h=MeshHandler(source);h.process();cache[mid]=(source,h,h.get_triangles())
   source,h,submeshes=cache[mid];batch=renderer['m_StaticBatchInfo'];count=batch['subMeshCount'];start=batch['firstSubMesh'] if count else 0;end=start+count if count else len(submeshes)
   if start<0 or end>len(submeshes):raise ValueError('invalid static submesh range')
   # Unbatched vertices already use renderer-local coordinates, even if that
   # object's authored world scale is zero. Batched data needs an inverse.
   matrix=mul(inverse_affine(world(tid)),batch_matrix(count,ref(renderer['m_StaticBatchRoot']),tid,world)) if count else identity();normal=normal_matrix(matrix)
   materials=renderer.get('m_Materials',[]);cells={}
   for sub in range(start,end):
    material=materials[min(sub-start,len(materials)-1)] if materials else {'m_FileID':0,'m_PathID':0};material_key=(material.get('m_FileID',0),material['m_PathID'])
    for triangle in submeshes[sub]:
     if len(triangle)!=3:raise ValueError('nontriangle topology')
     source_indices=[int(v)+int(source.m_SubMeshes[sub].baseVertex or 0) for v in triangle];local=[point(matrix,h.m_Vertices[v][:3]) for v in source_indices];original_world=[point(world(tid),p) for p in local]
     cell_key=tuple(math.floor(sum(p[c] for p in original_world)/3/a.cell_size) for c in range(3));cell=cells.setdefault((material_key,cell_key),{'vertices':[],'indices':[],'lookup':{}})
     for v,p in zip(source_indices,local):
      if not all(math.isfinite(x) and abs(x)<100000 for x in p):raise ValueError('invalid local render vertex')
      n=h.m_Normals[v][:3] if h.m_Normals else (0,1,0)
      if not all(math.isfinite(x) for x in n):raise ValueError('invalid source render normal')
      n=normalize(tuple(sum(normal[r][c]*n[c] for c in range(3)) for r in range(3)));uv=h.m_UV0[v][:2] if h.m_UV0 else (0,0)
      if not all(math.isfinite(x) for x in n):raise ValueError('invalid render normal')
      if not all(math.isfinite(x) for x in uv):uv=(0,0);counts['undefined_uv_replaced']+=1
      color=vertex_color(source,h,v)
      packed=struct.pack('<8fI',*p,*n,*uv,color);index=cell['lookup'].get(packed)
      if index is None:index=len(cell['vertices']);cell['lookup'][packed]=index;cell['vertices'].append(packed)
      cell['indices'].append(index)
   first_chunk=len(chunks)//48;owner_index=len(owners)
   for (material,cell_key),cell in sorted(cells.items()):
    if not cell['indices']:continue
    points=[struct.unpack_from('<3f',v) for v in cell['vertices']];lo=[min(p[c] for p in points) for c in range(3)];hi=[max(p[c] for p in points) for c in range(3)]
    chunks+=struct.pack('<IIqII6f',owner_index,material[0],material[1],index_count,len(cell['indices']),*lo,*hi)
    vertices.extend(b''.join(cell['vertices']));indices.extend(struct.pack('<'+'I'*len(cell['indices']),*(vertex_count+i for i in cell['indices'])));vertex_count+=len(cell['vertices']);index_count+=len(cell['indices'])
   chunk_count=len(chunks)//48-first_chunk
   if not chunk_count:counts['empty_mesh']+=1;continue
   flags=int(renderer.get('m_CastShadows',1))|(4 if renderer.get('m_ReceiveShadows') else 0)
   owners.append(struct.pack('<qq4I',renderer_id,go,first_chunk,chunk_count,flags,0));counts['retained_renderers']+=1;counts['initially_active' if active(tid) and renderer.get('m_Enabled') else 'initially_inactive']+=1
  except Exception as error:errors.append({'renderer':renderer_id,'object':go,'name':gos[go]['m_Name'],'error':str(error)})
 if errors:raise RuntimeError('Render ownership export failed: '+json.dumps(errors[:8]))
 if not vertex_count or not index_count or len(owners)>65536 or vertex_count>4000000 or index_count>12000000 or len(chunks)//48>262144:raise ValueError('render allocation budget exceeded')
 owner_bytes=b''.join(owners);payload=vertices+indices+owner_bytes+chunks;crc=zlib.crc32(payload)
 header=struct.pack('<8s6I6f2I',b'NXRENDER',1,vertex_count,index_count,len(chunks)//48,len(owners),camera['mask'],*camera['position'],camera['yaw'],camera['pitch'],camera['fov'],crc,gh[5])
 a.output.parent.mkdir(parents=True,exist_ok=True);temp=a.output.with_name(a.output.name+'.tmp');temp.write_bytes(header+payload);temp.replace(a.output)
 report={'format':'NXRENDER1','source_sha256':source_sha,'graph_crc':gh[5],'graph_sha256':hashlib.sha256(graph).hexdigest(),'output_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'counts':dict(counts),'vertices':vertex_count,'triangles':index_count//3,'owners':len(owners),'chunks':len(chunks)//48,'bytes':a.output.stat().st_size,'camera':camera,'externals':[entry.path for entry in objects[next(iter(objects))].assets_file.externals],'original_material_shader_texture_parity':False,'dynamic_actors_implemented':False,'conversion_errors':errors}
 if models_loaded:report['models_sha256']=hashlib.sha256(a.models_bundle.read_bytes()).hexdigest()
 if default_resources.is_file():report['default_resources_sha256']=hashlib.sha256(default_resources.read_bytes()).hexdigest()
 a.output.with_suffix('.render.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--bundle',type=Path,required=True);p.add_argument('--graph',type=Path,required=True);p.add_argument('--models-bundle',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-lib',type=Path);p.add_argument('--cell-size',type=float,default=32);a=p.parse_args()
 if not math.isfinite(a.cell_size) or a.cell_size<=0:p.error('cell size must be positive')
 export(a)
