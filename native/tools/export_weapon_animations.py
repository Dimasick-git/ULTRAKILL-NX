"""Retain weapon animation controllers, curves and original callback events.

The snapshot is a source for the native animation implementation, not a runtime
Animator replacement. No curves or callbacks are approximated during export.
"""
from pathlib import Path
import argparse,collections,gzip,hashlib,json,sys
from export_world import encode

def export(a):
 if a.python_lib:sys.path.insert(0,str(a.python_lib))
 import UnityPy
 from UnityPy.classes.PPtr import PPtr
 from bundle_index import Resolver
 source=a.weapons.read_bytes();weapons=json.loads(gzip.decompress(source))
 if weapons['format']!='NXWEAPON-SNAPSHOT1':raise ValueError('unsupported weapon snapshot')
 env=UnityPy.Environment();resolver=Resolver(env,a.bundles);loaded=set();records={};bindings=[];events=[];counts=collections.Counter()
 def identity(obj):return obj.assets_file.name.lower()+':'+str(obj.path_id)
 def pointer(value,file):return PPtr(m_FileID=value['m_FileID'],m_PathID=value['m_PathID'],assetsfile=file)
 def add(obj):
  key=identity(obj)
  if key in records:return key
  tree=obj.read_typetree();records[key]={'key':key,'id':str(obj.path_id),'file':obj.assets_file.name,'type':obj.type.name,'data':encode(tree),'externals':[e.path for e in obj.assets_file.externals]};counts[obj.type.name]+=1
  if obj.type.name=='AnimatorController':
   for clip in tree['m_AnimationClips']:
    if clip['m_PathID']:add(resolver.resolve(pointer(clip,obj.assets_file)))
   for behaviour in tree['m_StateMachineBehaviours']:
    if behaviour['m_PathID']:add(resolver.resolve(pointer(behaviour,obj.assets_file)))
  elif obj.type.name=='AnimatorOverrideController':
   add(resolver.resolve(pointer(tree['m_Controller'],obj.assets_file)))
   for pair in tree['m_Clips']:
    for name in ('m_OriginalClip','m_OverrideClip'):
     if pair[name]['m_PathID']:add(resolver.resolve(pointer(pair[name],obj.assets_file)))
  elif obj.type.name=='AnimationClip':
   muscle=tree['m_MuscleClip'];duration=muscle['m_StopTime']-muscle['m_StartTime']
   if duration<0 or tree['m_SampleRate']<=0:raise ValueError('invalid authored animation duration/rate')
   for event in tree['m_Events']:events.append({'clip':key,'name':tree['m_Name'],'duration':duration,'event':encode(event)})
  elif obj.type.name!='MonoBehaviour':raise ValueError('unexpected animation resource: '+obj.type.name)
  return key
 for prefab in weapons['prefabs']:
  bundle=Path(prefab['bundle'])
  if bundle not in loaded:
   if hashlib.sha256(bundle.read_bytes()).hexdigest()!=weapons['dependencies'][str(bundle)]:raise ValueError('weapon bundle identity changed')
   env.load_file(str(bundle));loaded.add(bundle);resolver.loaded.add(bundle)
  for record in prefab['records']:
   if record['type']!='Animator':continue
   file=env.get_cab(record['file']);obj=file.objects[int(record['id'])];tree=obj.read_typetree();value=tree['m_Controller']
   if not value['m_PathID']:continue
   controller=resolver.resolve(pointer(value,file));key=add(controller);bindings.append({'weapon_guid':prefab['guid'],'slot':prefab['slot'],'variant':prefab['variant'],'animator':record['key'],'controller':key})
 result={'format':'NXWEAPON-ANIMATION-SNAPSHOT1','weapons_sha256':hashlib.sha256(source).hexdigest(),'dependencies':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(loaded|resolver.dependencies)},'bindings':bindings,'records':list(records.values()),'events':events,'runtime_animation':False,'hardware_verified':False}
 data=json.dumps(result,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode();a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(gzip.compress(data,mtime=0));summary={'format':result['format'],'bindings':len(bindings),'resources':dict(counts),'events':len(events),'callbacks':dict(collections.Counter(e['event']['functionName'] for e in events)),'bytes':a.output.stat().st_size,'sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'runtime_animation':False};a.output.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--weapons',type=Path,required=True);p.add_argument('--bundles',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-lib',type=Path);export(p.parse_args())
