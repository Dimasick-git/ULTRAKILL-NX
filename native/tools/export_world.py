"""Preserve authored scene gameplay data for native system implementations.

This is migration data, not an interpreter for Unity scripts. Script class
names and fields survive; unsupported native behaviors remain explicitly
unimplemented in the coverage report.
"""
from pathlib import Path
import argparse,base64,collections,gzip,hashlib,json,math,sys

def encode(value):
 if isinstance(value,dict):
  if set(value)=={'m_FileID','m_PathID'}:
   return {'$ref':{'file':int(value['m_FileID']),'path':str(value['m_PathID'])}}
  return {key:encode(v) for key,v in value.items()}
 if isinstance(value,(list,tuple)):return [encode(v) for v in value]
 if isinstance(value,bytes):return {'$bytes':base64.b64encode(value).decode('ascii')}
 if isinstance(value,float) and not math.isfinite(value):return {'$float':'nan' if math.isnan(value) else 'inf' if value>0 else '-inf'}
 return value

def export(args):
 if args.python_lib:sys.path.insert(0,str(args.python_lib))
 import UnityPy
 env=UnityPy.load(str(args.bundle));objects=list(env.objects)
 env.load_file(str(args.scripts or args.bundle.parent/'monoscript_monoscripts.bundle'))
 # Preserve every authored GameObject component, including renderer/UI/audio
 # components, instead of silently dropping unknown component classes.
 required=set()
 for obj in objects:
  if obj.type.name=='GameObject':
   required.add(obj.path_id)
   for item in obj.read_typetree()['m_Component']:
    pointer=item.get('component',item.get('second',{}))
    if pointer.get('m_FileID',0)==0 and pointer.get('m_PathID'):required.add(pointer['m_PathID'])
 globals={'MonoBehaviour','RenderSettings','LightmapSettings','NavMeshSettings','OcclusionCullingSettings','LightingSettings'}
 records=[];scripts=collections.Counter();counts=collections.Counter();errors=[]
 for obj in objects:
  if obj.path_id not in required and obj.type.name not in globals:continue
  try:
   tree=obj.read_typetree();name=obj.type.name
   record={'id':str(obj.path_id),'type':name,'data':encode(tree)}
   if name=='MonoBehaviour':
    if tree['m_Script']['m_PathID']==0:
     record['script']=None;counts['SourceMissingScript']+=1
    else:
     script=obj.read().m_Script.read();record['script']={'class':script.m_ClassName,'namespace':script.m_Namespace,'assembly':script.m_AssemblyName}
     scripts['.'.join(v for v in (script.m_Namespace,script.m_ClassName) if v)]+=1
   records.append(record);counts[name]+=1
  except Exception as error:errors.append({'id':str(obj.path_id),'type':obj.type.name,'error':str(error)})
 if errors:raise RuntimeError('Gameplay data export failed: '+json.dumps(errors[:5]))
 first=objects[0].assets_file
 externals=[{'file':i+1,'path':entry.path} for i,entry in enumerate(first.externals)]
 data={'format':'NXWORLD-MIGRATION-1','bundle_sha256':hashlib.sha256(args.bundle.read_bytes()).hexdigest(),'externals':externals,'objects':records}
 # Reproducible gzip: no original filename or wall clock timestamp in header.
 args.output.parent.mkdir(parents=True,exist_ok=True);payload=json.dumps(data,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode('utf-8')
 temp=args.output.with_suffix('.tmp')
 with temp.open('wb') as file:
  with gzip.GzipFile(filename='',mode='wb',fileobj=file,mtime=0) as stream:stream.write(payload)
 temp.replace(args.output)
 report={'format':data['format'],'source_sha256':data['bundle_sha256'],'objects':dict(counts),'scripts':dict(sorted(scripts.items())),'script_types':len(scripts),'native_script_implementations':0,'complete_gameplay':False,'raw_bytes':len(payload),'compressed_bytes':args.output.stat().st_size,'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest()}
 args.output.with_suffix('.coverage.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
 print(json.dumps({k:report[k] for k in ('objects','script_types','raw_bytes','compressed_bytes','complete_gameplay')}))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--scripts',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-lib',type=Path);export(p.parse_args())
