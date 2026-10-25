"""Check actual batched mesh placement against a separate authored collider.

Uses a pinned nonidentity-transform object from TEST16, so accidentally applying
the renderer transform twice cannot pass through an identity-only fixture.
"""
from pathlib import Path
import argparse,hashlib,json,sys
from export_scene import identity,mul,trs,point,batch_matrix

def main():
 p=argparse.ArgumentParser();p.add_argument('--bundle',type=Path,required=True);p.add_argument('--python-lib',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();sys.path.insert(0,str(a.python_lib))
 import UnityPy
 from UnityPy.helpers.MeshHelper import MeshHandler
 source_hash=hashlib.sha256(a.bundle.read_bytes()).hexdigest()
 if source_hash!='896a6e586352e5e8ce136dc4736369a3c4eba337a09bf55c2cfbbf7e560d5a8b':raise ValueError('Audit fixture belongs to the archived TEST16 level 1-2 bundle')
 env=UnityPy.load(str(a.bundle));objects={o.path_id:o for o in env.objects};trees={}
 def tree(i):
  if i not in trees:trees[i]=objects[i].read_typetree()
  return trees[i]
 transforms={i:tree(i) for i,o in objects.items() if o.type.name=='Transform'};by_go={t['m_GameObject']['m_PathID']:i for i,t in transforms.items()};cache={}
 def world(i):
  if not i:return identity()
  if i not in cache:cache[i]=mul(world(transforms[i]['m_Father']['m_PathID']),trs(transforms[i]))
  return cache[i]
 def mesh(i):
  source=objects[i].read();h=MeshHandler(source);h.process();return source,h
 def bounds(vertices):return [[min(v[c] for v in vertices) for c in range(3)],[max(v[c] for v in vertices) for c in range(3)]]
 renderer=tree(25026);go=renderer['m_GameObject']['m_PathID'];assert go==2269
 batch=renderer['m_StaticBatchInfo'];source,h=mesh(720);triangles=h.get_triangles();indices=[int(i)+int(source.m_SubMeshes[s].baseVertex or 0) for s in range(batch['firstSubMesh'],batch['firstSubMesh']+batch['subMeshCount']) for tri in triangles[s] for i in tri]
 root=renderer['m_StaticBatchRoot']['m_PathID'];assert batch['subMeshCount']>0 and root==0
 converted=bounds([point(batch_matrix(batch['subMeshCount'],root,by_go[go],world),h.m_Vertices[i][:3]) for i in indices])
 _,collision=mesh(1446);expected=bounds([point(world(by_go[go]),v[:3]) for v in collision.m_Vertices])
 error=max(abs(x-y) for a,b in zip(converted,expected) for x,y in zip(a,b))
 if error>.001:raise AssertionError(('Batched render/collider bounds differ',converted,expected))
 old=bounds([point(world(by_go[go]),h.m_Vertices[i][:3]) for i in indices]);assert max(abs(x-y) for a,b in zip(old,expected) for x,y in zip(a,b))>100
 report={'source_sha256':source_hash,'renderer':25026,'object':go,'null_batch_root':True,'render_bounds':converted,'independent_collider_world_bounds':expected,'maximum_error':error,'incorrect_double_transform_bounds':old,'scope':'Original static-batch coordinate space; not material, shader or complete visual parity'}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS: archived static batch matches independent authored collider bounds, max error',error)

if __name__=='__main__':main()
