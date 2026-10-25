"""Preserve exact authored weapon hierarchies referenced by GunSetter.

This resource snapshot is input for native instantiation, animation, rendering
and combat systems. It does not execute the recovered scripts by itself.
"""
from pathlib import Path
import argparse,collections,gzip,hashlib,json,sys
from addressable_catalog import Catalog
from export_world import encode

def export(a):
 if a.python_lib:sys.path.insert(0,str(a.python_lib))
 import UnityPy
 from UnityPy.classes.PPtr import PPtr
 from bundle_index import Resolver
 source=a.world.read_bytes();world=json.loads(gzip.decompress(source));setters=[o for o in world['objects'] if (o.get('script') or {}).get('class')=='GunSetter']
 if len(setters)!=1:raise ValueError('expected one original GunSetter')
 fields=('revolverPierce','revolverRicochet','revolverTwirl','shotgunGrenade','shotgunPump','shotgunRed','nailMagnet','nailOverheat','nailRed','railCannon','railHarpoon','railMalicious','rocketBlue','rocketGreen','rocketRed')
 catalog=Catalog(a.catalog);env=UnityPy.Environment();resolver=Resolver(env,a.catalog.parent/'StandaloneWindows64');loaded=set();prefabs=[];allcounts=collections.Counter()
 def identity(obj):return obj.assets_file.name.lower()+':'+str(obj.path_id)
 def pointer(data,file):return PPtr(m_FileID=data['m_FileID'],m_PathID=data['m_PathID'],assetsfile=file)
 for field in fields:
  for variant,reference in enumerate(setters[0]['data'][field]):
   guid=reference['m_AssetGUID'];locations=catalog.locate(guid)
   if len(locations)!=1 or reference['m_SubObjectName'] or reference['m_SubObjectType']:raise ValueError('ambiguous/subobject weapon asset reference')
   root=None;bundle=None
   for dep in catalog.dependencies(locations[0]):
    candidate=catalog.bundle_path(dep)
    if candidate not in loaded:env.load_file(str(candidate));loaded.add(candidate);resolver.loaded.add(candidate)
    # Environment.container walks every preload dependency and may recursively
    # scan the working directory for missing CAB files. Read the explicit
    # serialized-file container instead; Resolver owns dependency resolution.
    matches=[file.container.container_dict[guid] for file in env.cabs.values() if hasattr(file,'container') and guid in file.container.container_dict]
    if len(matches)>1:raise ValueError('ambiguous weapon GUID containers')
    if matches:root=resolver.resolve(matches[0]);bundle=candidate;break
   if root is None or root.type.name!='GameObject':raise ValueError('weapon GUID missing from exact catalog bundle containers')
   records={};pending=[root];objects=[];types=collections.Counter();scripts=collections.Counter();revolvers=[]
   def add(obj):
    key=identity(obj)
    if key in records:return records[key]
    tree=obj.read_typetree();record={'key':key,'id':str(obj.path_id),'file':obj.assets_file.name,'type':obj.type.name,'data':encode(tree),'externals':[e.path for e in obj.assets_file.externals]};records[key]=record;types[obj.type.name]+=1
    if obj.type.name=='MonoBehaviour' and tree['m_Script']['m_PathID']:
     script=resolver.resolve(pointer(tree['m_Script'],obj.assets_file));st=script.read_typetree();record['script']={'class':st['m_ClassName'],'namespace':st['m_Namespace'],'assembly':st['m_AssemblyName']};scripts[st['m_ClassName']]+=1
     if st['m_ClassName']=='Revolver':revolvers.append(record['key'])
    return record
   seen=set()
   while pending:
    go=pending.pop();key=identity(go)
    if key in seen:raise ValueError('prefab hierarchy cycle/duplicate child')
    seen.add(key);gt=go.read_typetree();add(go);transform=None
    for item in gt['m_Component']:
     value=item.get('component',item.get('second'));component=resolver.resolve(pointer(value,go.assets_file));add(component)
     if component.type.name in ('Transform','RectTransform'):
      if transform is not None:raise ValueError('multiple prefab transforms')
      transform=component
    if transform is None:raise ValueError('prefab transform missing')
    for child in transform.read_typetree()['m_Children']:
     ct=resolver.resolve(pointer(child,transform.assets_file));cgo=resolver.resolve(pointer(ct.read_typetree()['m_GameObject'],ct.assets_file));pending.append(cgo)
   if field.startswith('revolver') and len(revolvers)!=1:raise ValueError('revolver prefab must retain one Revolver component')
   counts={'objects':len(seen),'records':len(records),'types':dict(types),'scripts':dict(scripts)};allcounts.update(types)
   root_name=records[identity(root)]['data']['m_Name'];prefabs.append({'slot':field,'variant':variant,'guid':guid,'name':root_name,'catalog_location':locations[0],'internal_id':catalog.internal(locations[0]),'bundle':str(bundle),'root':identity(root),'counts':counts,'revolver_components':revolvers,'records':list(records.values())});print(field,variant,root_name,counts['objects'],counts['records'],flush=True)
 dependencies=loaded|resolver.dependencies
 result={'format':'NXWEAPON-SNAPSHOT1','world_sha256':hashlib.sha256(source).hexdigest(),'catalog_sha256':hashlib.sha256(a.catalog.read_bytes()).hexdigest(),'gun_setter':setters[0]['id'],'dependencies':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(dependencies)},'prefabs':prefabs,'runtime_instantiation':False,'gameplay_complete':False}
 encoded=json.dumps(result,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode();a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(gzip.compress(encoded,mtime=0));summary={'format':result['format'],'prefabs':len(prefabs),'revolvers':sum(bool(p['revolver_components']) for p in prefabs),'component_types':dict(allcounts),'bytes':a.output.stat().st_size,'sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'runtime_instantiation':False};a.output.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--world',type=Path,required=True);p.add_argument('--catalog',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-lib',type=Path);export(p.parse_args())
